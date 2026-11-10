#pragma once
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#if defined(__ANDROID__)
#include <pthread.h>
#endif

namespace rex::graphics::xnative {

// Drops batches of shared_ptr owners on a background thread. Used for frame
// state the render worker no longer reads (its release cost - atomic
// decrements and frees - is pure overhead on the frame's critical path).
template <typename T>
class NativeDeferredReleaser {
 public:
  using Batch = std::vector<std::shared_ptr<const T>>;
  NativeDeferredReleaser() : thread_([this] { Run(); }) {}
  ~NativeDeferredReleaser() {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
    }
    condition_.notify_one();
    thread_.join();
  }
  NativeDeferredReleaser(const NativeDeferredReleaser&) = delete;
  NativeDeferredReleaser& operator=(const NativeDeferredReleaser&) = delete;
  // Bounded like NativeCommandRetirer's backlog (native_owned_commands.h):
  // past the bound the releasing thread drops the oldest batches itself, so a
  // starved release thread cannot hold an unlimited amount of frame state.
  static constexpr size_t kMaximumPendingOwners = 32768;
  void Release(Batch&& batch) {
    if (batch.empty()) return;
    std::unique_lock lock(mutex_);
    pending_owners_ += batch.size();
    pending_.push_back(std::move(batch));
    while (pending_owners_ > kMaximumPendingOwners && !pending_.empty()) {
      Batch oldest = std::move(pending_.front());
      pending_.pop_front();
      pending_owners_ -= oldest.size();
      lock.unlock();
      oldest.clear();
      lock.lock();
    }
    const bool wake = !pending_.empty();
    lock.unlock();
    if (wake) condition_.notify_one();
  }

 private:
  void Run() {
#if defined(__ANDROID__)
    // Not its creator's name: runtime/os/thread_policy.cpp places threads by name.
    pthread_setname_np(pthread_self(), "XpReleaser");
#endif
    std::unique_lock lock(mutex_);
    for (;;) {
      condition_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
      if (pending_.empty()) return;
      Batch batch = std::move(pending_.front());
      pending_.pop_front();
      pending_owners_ -= batch.size();
      lock.unlock();
      batch.clear();
      lock.lock();
    }
  }
  std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<Batch> pending_;
  size_t pending_owners_ = 0;  // In pending_, under mutex_.
  bool stopping_ = false;
  std::thread thread_;
};

}  // namespace rex::graphics::xnative

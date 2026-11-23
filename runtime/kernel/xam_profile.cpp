// profile settings of the one local profile (user 0). Layout and sizing follow Xenia's
// XamUserReadProfileSettingsEx (BSD): an 8-byte header, then one 40-byte X_USER_PROFILE_SETTING
// per requested id, then the binary/string payloads. The title reads seven integer options plus its
// 1000-byte title-specific block; it first calls with no buffer and needs ERROR_INSUFFICIENT_BUFFER
// with the size, then allocates and reads asynchronously.
//
// Values written by the game are saved to save/profile.bin and loaded back on the next launch.
// Gears of War 3 keeps its options there (brightness, controls, ...): kept only in memory, every
// launch looked like a new profile and the game asked for the brightness again (issues #6, #7).
#include <stdafx.h>
#include <kernel/function.h>
#include <kernel/xdm.h>
#include <os/logger.h>
#include <os/paths.h>

uint32_t CompleteOverlapped(XXOVERLAPPED* overlapped, uint32_t result, uint32_t length);

namespace
{
    constexpr uint32_t ERROR_INSUFFICIENT_BUFFER_X = 122;
    constexpr uint32_t ERROR_NO_SUCH_USER_X = 0x525;

    // Setting ids: bits 28-31 data type, bits 16-27 maximum size, low bits the id itself.
    enum : uint8_t
    {
        TYPE_INT32 = 1,
        TYPE_INT64 = 2,
        TYPE_DOUBLE = 3,
        TYPE_UNICODE = 4,
        TYPE_FLOAT = 5,
        TYPE_BINARY = 6,
        TYPE_DATETIME = 7,
    };

    uint8_t SettingType(uint32_t id)
    {
        return id >> 28;
    }

    uint32_t SettingMaxSize(uint32_t id)
    {
        return (id >> 16) & 0xFFF;
    }

    bool HasPayload(uint32_t id)
    {
        return SettingType(id) == TYPE_BINARY || SettingType(id) == TYPE_UNICODE;
    }

#pragma pack(push, 1)
    struct UserData
    {
        uint8_t type;
        uint8_t padding[7];
        union
        {
            be<int32_t> s32;
            be<int64_t> s64;
            be<uint64_t> u64;
            struct
            {
                be<uint32_t> size;
                be<uint32_t> pointer;
            } blob;
        };
    };

    struct ProfileSetting
    {
        be<uint32_t> source;        // 0 = no value, 1 = default, 2 = written by the title
        be<uint32_t> padding0;
        be<uint32_t> userIndex;     // union with a 64-bit xuid on the console
        be<uint32_t> padding1;
        be<uint32_t> settingId;
        be<uint32_t> padding2;
        UserData data;
    };

    struct ReadResults
    {
        be<uint32_t> settingCount;
        be<uint32_t> settings;      // guest pointer to the ProfileSetting array
    };
#pragma pack(pop)

    static_assert(sizeof(UserData) == 16);
    static_assert(sizeof(ProfileSetting) == 40);
    static_assert(sizeof(ReadResults) == 8);

    struct StoredSetting
    {
        uint32_t source = 1;
        uint64_t value = 0;             // integer types
        std::vector<uint8_t> payload;   // binary and string types
    };

    std::mutex g_settingsMutex;
    std::unordered_map<uint32_t, StoredSetting> g_settings = {
        // Dashboard defaults Xenia uses for a new profile.
        { 0x10040003, { 1, 3 } }, // XPROFILE_OPTION_CONTROLLER_VIBRATION: on
        { 0x10040022, { 1, 1 } }, // XPROFILE_GAMER_ACTION_AUTO_AIM: on
    };

    // save/profile.bin, host byte order: "XPRF", version, count, then for each setting the title
    // wrote: id, source, value, payload size and payload. Defaults are not stored.
    constexpr uint32_t PROFILE_MAGIC = 0x46525058; // "XPRF"
    constexpr uint32_t PROFILE_VERSION = 1;
    constexpr uint32_t PROFILE_MAX_SETTINGS = 4096;

    std::filesystem::path ProfilePath()
    {
        return os::paths::GetSavePath() / "profile.bin";
    }

    // Once, with g_settingsMutex held, before the first read or write. A file that does not
    // parse is left alone (and replaced at the next write), so the game starts from the defaults.
    void LoadSettings()
    {
        static bool s_loaded = false;
        if (s_loaded)
            return;
        s_loaded = true;

        const std::filesystem::path path = ProfilePath();
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return;

        const auto read = [&file](auto& value)
        {
            return bool(file.read(reinterpret_cast<char*>(&value), sizeof(value)));
        };

        uint32_t magic = 0, version = 0, count = 0;
        if (!read(magic) || !read(version) || !read(count) || magic != PROFILE_MAGIC || version != PROFILE_VERSION ||
            count > PROFILE_MAX_SETTINGS)
        {
            LOGFN_WARNING("profile: {} is not a profile file, ignored", path.string());
            return;
        }

        std::unordered_map<uint32_t, StoredSetting> loaded;
        for (uint32_t i = 0; i < count; i++)
        {
            uint32_t id = 0, source = 0, size = 0;
            uint64_t value = 0;
            if (!read(id) || !read(source) || !read(value) || !read(size) || size > SettingMaxSize(id))
            {
                LOGFN_WARNING("profile: {} is damaged at setting {}, ignored", path.string(), i);
                return;
            }
            StoredSetting setting;
            setting.source = source;
            setting.value = value;
            setting.payload.resize(size);
            if (size != 0 && !file.read(reinterpret_cast<char*>(setting.payload.data()), size))
            {
                LOGFN_WARNING("profile: {} is damaged at setting {}, ignored", path.string(), i);
                return;
            }
            loaded[id] = std::move(setting);
        }

        for (auto& [id, setting] : loaded)
            g_settings[id] = std::move(setting);
        LOGFN("profile: {} settings loaded from {}", loaded.size(), path.string());
    }

    // With g_settingsMutex held. Written beside the old file and renamed over it, so closing the
    // app mid-write leaves the previous profile.
    void SaveSettings()
    {
        const std::filesystem::path path = ProfilePath();
        std::filesystem::path temp = path;
        temp += ".tmp";

        uint32_t count = 0;
        for (const auto& [id, setting] : g_settings)
            count += setting.source == 2;

        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            const auto write = [&file](const auto& value)
            {
                file.write(reinterpret_cast<const char*>(&value), sizeof(value));
            };
            write(PROFILE_MAGIC);
            write(PROFILE_VERSION);
            write(count);
            for (const auto& [id, setting] : g_settings)
            {
                if (setting.source != 2)
                    continue;
                write(id);
                write(setting.source);
                write(setting.value);
                write(uint32_t(setting.payload.size()));
                file.write(reinterpret_cast<const char*>(setting.payload.data()), std::streamsize(setting.payload.size()));
            }
            file.flush();
            if (!file)
            {
                LOGFN_WARNING("profile: cannot write {}", temp.string());
                return;
            }
        }

        std::error_code ec;
        std::filesystem::rename(temp, path, ec);
        if (ec)
            LOGFN_WARNING("profile: cannot replace {}: {}", path.string(), ec.message());
    }

    // A title-specific block that was never written has no value, like on a fresh console profile.
    StoredSetting Lookup(uint32_t id)
    {
        auto it = g_settings.find(id);
        if (it != g_settings.end())
            return it->second;

        StoredSetting setting;
        setting.source = HasPayload(id) ? 0 : 1;
        return setting;
    }
}

uint32_t XamUserReadProfileSettings(uint32_t titleId, uint32_t userIndex, uint32_t xuidCount, be<uint64_t>* xuids,
    uint32_t settingCount, be<uint32_t>* settingIds, be<uint32_t>* bufferSize, uint8_t* buffer, XXOVERLAPPED* overlapped)
{
    if (xuidCount == 0 && userIndex != 0 && userIndex != 0xFF)
        return ERROR_NO_SUCH_USER_X;

    std::lock_guard guard{ g_settingsMutex };
    LoadSettings();

    uint32_t needed = sizeof(ReadResults) + settingCount * sizeof(ProfileSetting);
    for (uint32_t i = 0; i < settingCount; i++)
    {
        if (HasPayload(settingIds[i]))
            needed += uint32_t(Lookup(settingIds[i]).payload.size());
    }

    const uint32_t available = bufferSize ? uint32_t(*bufferSize) : 0;
    if (bufferSize)
        *bufferSize = needed;

    if (buffer == nullptr || available < needed)
    {
        if (overlapped)
            return CompleteOverlapped(overlapped, ERROR_INSUFFICIENT_BUFFER_X, 0);
        return ERROR_INSUFFICIENT_BUFFER_X;
    }

    memset(buffer, 0, needed);
    auto* header = reinterpret_cast<ReadResults*>(buffer);
    auto* settings = reinterpret_cast<ProfileSetting*>(buffer + sizeof(ReadResults));
    uint32_t payloadOffset = sizeof(ReadResults) + settingCount * sizeof(ProfileSetting);

    header->settingCount = settingCount;
    header->settings = g_memory.MapVirtual(settings);

    for (uint32_t i = 0; i < settingCount; i++)
    {
        const uint32_t id = settingIds[i];
        const StoredSetting stored = Lookup(id);
        ProfileSetting& out = settings[i];

        out.source = stored.source;
        out.userIndex = xuidCount == 0 ? (userIndex == 0xFF ? 0 : userIndex) : 0;
        out.settingId = id;
        out.data.type = SettingType(id);

        if (HasPayload(id))
        {
            out.data.blob.size = uint32_t(stored.payload.size());
            out.data.blob.pointer = stored.payload.empty() ? 0 : g_memory.MapVirtual(buffer + payloadOffset);
            memcpy(buffer + payloadOffset, stored.payload.data(), stored.payload.size());
            payloadOffset += uint32_t(stored.payload.size());
        }
        else if (SettingType(id) == TYPE_INT32 || SettingType(id) == TYPE_FLOAT)
        {
            out.data.s32 = int32_t(stored.value);
        }
        else
        {
            out.data.u64 = stored.value;
        }
    }

    if (overlapped)
        return CompleteOverlapped(overlapped, ERROR_SUCCESS, 0);
    return ERROR_SUCCESS;
}

uint32_t XamUserWriteProfileSettings(uint32_t titleId, uint32_t userIndex, uint32_t settingCount,
    ProfileSetting* settings, XXOVERLAPPED* overlapped)
{
    {
        std::lock_guard guard{ g_settingsMutex };
        LoadSettings();
        for (uint32_t i = 0; i < settingCount; i++)
        {
            const ProfileSetting& in = settings[i];
            const uint32_t id = in.settingId;
            StoredSetting stored;
            stored.source = 2;

            if (HasPayload(id))
            {
                const uint32_t size = std::min<uint32_t>(in.data.blob.size, SettingMaxSize(id));
                const auto* data = in.data.blob.pointer ? static_cast<const uint8_t*>(g_memory.Translate(in.data.blob.pointer)) : nullptr;
                if (data)
                    stored.payload.assign(data, data + size);
            }
            else if (SettingType(id) == TYPE_INT32 || SettingType(id) == TYPE_FLOAT)
            {
                stored.value = uint32_t(int32_t(in.data.s32));
            }
            else
            {
                stored.value = in.data.u64;
            }

            g_settings[id] = std::move(stored);
        }
        SaveSettings();
    }

    LOGF("{} settings written", settingCount);

    if (overlapped)
        return CompleteOverlapped(overlapped, ERROR_SUCCESS, 0);
    return ERROR_SUCCESS;
}

GUEST_FUNCTION_HOOK(__imp__XamUserReadProfileSettings, XamUserReadProfileSettings);
GUEST_FUNCTION_HOOK(__imp__XamUserWriteProfileSettings, XamUserWriteProfileSettings);

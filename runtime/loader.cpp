// loads default.xex into guest memory and sets up what the kernel loader would:
// the import address table, the kernel's exported variables, the executable module and its
// XEX header (RtlImageXexHeaderField, XexGetModuleHandle). Variable layouts follow Xenia's
// xboxkrnl_module.cc; UnleashedRecomp left these as stubs.
#include <stdafx.h>
#include <file.h>
#include <image.h>
#include <xex.h>
#include <kernel/function.h>
#include <kernel/heap.h>
#include <kernel/memory.h>
#include "loader.h"

extern std::unordered_map<size_t, const char*> XamExports;
extern std::unordered_map<size_t, const char*> XboxKernelExports;

static uint32_t g_xexHeader;       // guest copy of the XEX header
static uint32_t g_executableModule; // fake LDR_DATA_TABLE_ENTRY
static uint8_t* g_keTimeStampBundle;
static std::unique_ptr<std::thread> g_timeStampThread;

static uint32_t AllocGuest(size_t size)
{
    void* ptr = g_userHeap.AllocPhysical(size, 16);
    memset(ptr, 0, size);
    return g_memory.MapVirtual(ptr);
}

template<typename T>
static void StoreBE(uint32_t address, T value)
{
    *(T*)g_memory.Translate(address) = ByteSwap(value);
}

static uint64_t GuestUptimeMillis()
{
    static const auto s_start = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s_start).count();
}

// Address of the guest storage behind a kernel variable export (by xboxkrnl ordinal).
static uint32_t KernelVariable(const std::string_view& name)
{
    if (name == "XboxKrnlVersion")
    {
        uint32_t p = AllocGuest(8);
        StoreBE<uint16_t>(p + 0, 2);
        StoreBE<uint16_t>(p + 2, 0xFFFF);
        StoreBE<uint16_t>(p + 4, 0xFFFF);
        *(uint8_t*)g_memory.Translate(p + 6) = 0x80;
        return p;
    }
    if (name == "KeTimeStampBundle")
    {
        // InterruptTime, SystemTime, TickCount (ms), padding; TickCount is refreshed every 1 ms.
        uint32_t p = AllocGuest(24);
        g_keTimeStampBundle = (uint8_t*)g_memory.Translate(p);
        StoreBE<uint32_t>(p + 16, uint32_t(GuestUptimeMillis()));
        return p;
    }
    if (name == "ExLoadedCommandLine")
    {
        uint32_t p = AllocGuest(1024);
        strcpy((char*)g_memory.Translate(p), "\"default.xex\"");
        return p;
    }
    if (name == "XexExecutableModuleHandle")
    {
        uint32_t p = AllocGuest(4);
        StoreBE<uint32_t>(p, g_executableModule);
        return p;
    }
    if (name == "KeDebugMonitorData" || name == "KeCertMonitorData")
        return AllocGuest(4); // pointer to zero: no debugger
    if (name == "VdGpuClockInMHz")
    {
        uint32_t p = AllocGuest(4);
        StoreBE<uint32_t>(p, 500);
        return p;
    }
    if (name == "VdGlobalDevice")
        return AllocGuest(4); // D3D stores its device pointer here
    if (name == "VdHSIOCalibrationLock")
        return AllocGuest(28); // RTL_CRITICAL_SECTION, unlocked
    if (name == "ExThreadObjectType")
        return AllocGuest(64); // only compared/passed through by ObReferenceObjectByHandle

    LOGF_WARNING("no storage for kernel variable {}", name);
    return AllocGuest(64);
}

static void StartTimeStampThread()
{
    g_timeStampThread = std::make_unique<std::thread>([]
    {
        auto next = std::chrono::steady_clock::now();
        while (true)
        {
            *(be<uint32_t>*)(g_keTimeStampBundle + 16) = uint32_t(GuestUptimeMillis());
            next += std::chrono::milliseconds(1);
            std::this_thread::sleep_until(next);
        }
    });
}

static void SetupModule(const uint8_t* xex)
{
    auto* header = reinterpret_cast<const Xex2Header*>(xex);
    g_xexHeader = AllocGuest(header->headerSize);
    memcpy(g_memory.Translate(g_xexHeader), xex, header->headerSize);

    // Xenia's layout: the xex header pointer sits at +0x58 of the module structure.
    g_executableModule = AllocGuest(0x100);
    StoreBE<uint32_t>(g_executableModule + 0x58, g_xexHeader);
}

static void BindImports(const uint8_t* xex, const Image& image)
{
    auto* imports = reinterpret_cast<const Xex2ImportHeader*>(getOptHeaderPtr(xex, XEX_HEADER_IMPORT_LIBRARIES));
    if (!imports)
        return;

    std::vector<std::string> names;
    const char* strings = reinterpret_cast<const char*>(imports + 1);
    for (size_t i = 0, offset = 0; i < imports->numImports; i++)
    {
        names.emplace_back(strings + offset);
        offset += ((names.back().size() + 1) + 3) & ~3;
    }

    auto* library = reinterpret_cast<const Xex2ImportLibrary*>(reinterpret_cast<const char*>(imports) + sizeof(Xex2ImportHeader) + imports->sizeOfStringTable);
    size_t variables = 0, functions = 0;
    for (size_t i = 0; i < names.size(); i++)
    {
        const std::string& libraryName = names[library->name];
        const auto& table = libraryName == "xam.xex" ? XamExports : XboxKernelExports;
        auto* descriptors = reinterpret_cast<const Xex2ImportDescriptor*>(library + 1);

        for (size_t k = 0; k < library->numberOfImports; k++)
        {
            // Xex2LoadImage left variable records byteswapped in the image and replaced function
            // thunks with a stub; a record followed by a thunk is a function import.
            uint32_t address = descriptors[k].firstThunk;
            uint32_t record;
            memcpy(&record, image.Find(address), 4);
            if (record == 0x00000060)
                continue;

            uint32_t ordinal = record & 0xFFFF;
            auto it = table.find(ordinal);
            std::string_view name = it != table.end() ? std::string_view(it->second).substr(strlen("__imp__")) : "?";

            uint32_t next = 0;
            if (k + 1 < library->numberOfImports)
                memcpy(&next, image.Find(descriptors[k + 1].firstThunk), 4);

            if (next == 0x00000060)
            {
                // The IAT slot gets the thunk, whose address maps to the __imp__ function.
                StoreBE<uint32_t>(address, descriptors[k + 1].firstThunk);
                functions++;
            }
            else
            {
                StoreBE<uint32_t>(address, KernelVariable(name));
                variables++;
            }
        }
        library = reinterpret_cast<const Xex2ImportLibrary*>(reinterpret_cast<const char*>(library + 1) + library->numberOfImports * sizeof(Xex2ImportDescriptor));
    }
    LOGF("bound {} function and {} variable imports", functions, variables);
}

uint32_t LdrLoadModule(const std::filesystem::path& path)
{
    const auto file = LoadFile(path);
    if (file.empty())
    {
        LOGF_ERROR("cannot read {}", path.string());
        return 0;
    }

    // XenonUtils decrypts (retail AES) and decompresses (basic/LZX) the image.
    Image image = Image::ParseImage(file.data(), file.size());
    if (!image.data)
    {
        LOGF_ERROR("cannot load image from {}", path.string());
        return 0;
    }

    size_t end = 0;
    for (const auto& section : image.sections)
        end = std::max(end, section.base - image.base + section.size);
    // The title's .reloc claims more than the image holds (it ends at the image size).
    end = std::min<size_t>(end, image.size);
    memcpy(g_memory.Translate(image.base), image.data.get(), end);

    SetupModule(file.data());
    BindImports(file.data(), image);
    StartTimeStampThread();

    LOGF("loaded {} at 0x{:08X}, entry 0x{:08X}", path.filename().string(), image.base, image.entry_point);
    return uint32_t(image.entry_point);
}

// Optional header lookup on a guest XEX header (Xenia's UserModule::GetOptHeader):
// key low byte 0x00 returns the value, 0x01 a pointer to the value, otherwise a pointer to the data.
static uint32_t GetOptHeader(uint32_t xexHeader, uint32_t key)
{
    auto* header = reinterpret_cast<const Xex2Header*>(g_memory.Translate(xexHeader));
    auto* opts = reinterpret_cast<const Xex2OptHeader*>(header + 1);
    for (uint32_t i = 0; i < header->headerCount; i++)
    {
        if (opts[i].key != key)
            continue;
        switch (key & 0xFF)
        {
        case 0x00: return opts[i].value;
        case 0x01: return g_memory.MapVirtual((void*)&opts[i].value);
        default: return xexHeader + opts[i].offset;
        }
    }
    return 0;
}

uint32_t RtlImageXexHeaderField(uint32_t xexHeader, uint32_t key)
{
    return GetOptHeader(xexHeader ? xexHeader : g_xexHeader, key);
}

uint32_t XexGetModuleHandle(const char* moduleName, be<uint32_t>* moduleHandle)
{
    // Only the executable is a module here; xam.xex/xboxkrnl.exe exports are reached through imports.
    if (moduleName && strcmp(moduleName, "default.xex") != 0)
    {
        LOGF_WARNING("module {} not found", moduleName);
        *moduleHandle = 0;
        return 0x00000490; // X_ERROR_NOT_FOUND
    }
    *moduleHandle = g_executableModule;
    return 0;
}

uint32_t XexGetProcedureAddress(uint32_t moduleHandle, uint32_t ordinal, be<uint32_t>* functionPtr)
{
    if (ordinal & 0xFFFF0000)
        LOGF_WARNING("by name {} (module 0x{:X}): not found", (const char*)g_memory.Translate(ordinal), moduleHandle);
    else
        LOGF_WARNING("ordinal {} (module 0x{:X}): not found", ordinal, moduleHandle);
    *functionPtr = 0;
    return 0xC0000139; // X_STATUS_ENTRYPOINT_NOT_FOUND
}

// Titles load system modules by name at run time: Gears of War 3's XPARTY library loads xam.xex
// and asks it for the XamParty* exports by ordinal. There is no module here but the executable, so
// the file is not there (Xenia's answer for a module it lacks): the caller's wrapper (82CE5CB0 in
// Gears 3) then returns a null handle instead of the one the generated stub never wrote.
uint32_t XexLoadImage(const char* moduleName, uint32_t flags, uint32_t minVersion, be<uint32_t>* moduleHandle)
{
    LOGF_WARNING("{} (flags 0x{:X}, version 0x{:X}): not loaded", moduleName ? moduleName : "(null)", flags, minVersion);
    if (moduleHandle)
        *moduleHandle = 0;
    return 0xC000000F; // X_STATUS_NO_SUCH_FILE
}

uint32_t XexUnloadImage(uint32_t moduleHandle)
{
    return 0; // nothing was loaded
}

#if XPORT_TITLE_ID == 0x4D5308AB
// A resource of the XEX (XEX_HEADER_RESOURCE_INFO: 8-byte name, address, size), already in the
// image. Gears of War 3 asks for "HashSec" (its files' SHA-1 list, 0x835A0000) at boot (8295B5E8)
// and parses it when the call succeeds; the stub in kernel/imports.cpp returned success with
// neither pointer written, so it parsed what was on the stack. Only for this title: the other
// games keep the stub their finished ports were built with.
uint32_t XexGetModuleSection(uint32_t moduleHandle, const char* name, be<uint32_t>* data, be<uint32_t>* size)
{
    if (moduleHandle != g_executableModule)
        LOGF_WARNING("module 0x{:X} is not the executable's (0x{:X}): looking in it anyway", moduleHandle, g_executableModule);
    auto* xex = reinterpret_cast<const uint8_t*>(g_memory.Translate(g_xexHeader));
    auto* info = reinterpret_cast<const uint8_t*>(getOptHeaderPtr(xex, XEX_HEADER_RESOURCE_INFO));
    if (info && name)
    {
        const uint32_t count = (ByteSwap(*reinterpret_cast<const uint32_t*>(info)) - 4) / 16;
        for (uint32_t i = 0; i < count; i++)
        {
            const uint8_t* entry = info + 4 + 16 * i;
            if (strncmp(reinterpret_cast<const char*>(entry), name, 8) != 0)
                continue;
            const uint32_t address = ByteSwap(*reinterpret_cast<const uint32_t*>(entry + 8));
            const uint32_t bytes = ByteSwap(*reinterpret_cast<const uint32_t*>(entry + 12));
            if (data)
                *data = address;
            if (size)
                *size = bytes;
            LOGF("{}: 0x{:08X}, {} bytes", name, address, bytes);
            return 0;
        }
    }
    LOGF_WARNING("{}: no such resource", name ? name : "(null)");
    return 0xC0000001; // X_STATUS_UNSUCCESSFUL, as Xenia's UserModule::GetSection
}

GUEST_FUNCTION_HOOK(__imp__XexGetModuleSection, XexGetModuleSection);

// The executable's XEX_HEADER_EXECUTION_INFO (media ID, version, base version, title ID, ...), in the
// guest copy of the header. Gears of War 3 checks a title ID against it before
// XamUserReadProfileSettings (82CE9108); the stub left the pointer unwritten and returned the
// pointer's address in r3 (not negative: success), so it read through stack garbage.
uint32_t XamGetExecutionId(be<uint32_t>* info)
{
    const uint32_t address = GetOptHeader(g_xexHeader, 0x00040006); // XEX_HEADER_EXECUTION_INFO
    if (info)
        *info = address;
    return address ? 0 : 0xC0000001; // X_STATUS_UNSUCCESSFUL
}

GUEST_FUNCTION_HOOK(__imp__XamGetExecutionId, XamGetExecutionId);
#endif

GUEST_FUNCTION_HOOK(__imp__RtlImageXexHeaderField, RtlImageXexHeaderField);
GUEST_FUNCTION_HOOK(__imp__XexLoadImage, XexLoadImage);
GUEST_FUNCTION_HOOK(__imp__XexUnloadImage, XexUnloadImage);
GUEST_FUNCTION_HOOK(__imp__XexGetModuleHandle, XexGetModuleHandle);
GUEST_FUNCTION_HOOK(__imp__XexGetProcedureAddress, XexGetProcedureAddress);

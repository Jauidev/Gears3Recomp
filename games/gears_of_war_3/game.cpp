// Gears of War 3: what the runtime asks of the game (runtime/game.h).
#include <stdafx.h>
#include <game.h>
#include <kernel/function.h>
#include <kernel/memory.h>
#include <os/logger.h>
#include <os/paths.h>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>

uint32_t MmQueryAddressProtect(uint32_t address); // kernel/vmem.cpp

extern "C" PPC_FUNC(__imp__sub_829815C8);

// sub_829815C8: Evita crash signal 11 cuando [r3 + 4] es nulo (0x00000000)
PPC_FUNC(sub_829815C8)
{
    if (ctx.r3.u32 == 0 || PPC_LOAD_U32(ctx.r3.u32 + 4) == 0)
    {
        static std::atomic<int> s_logged{ 0 };
        if (s_logged.fetch_add(1) < 4)
            LOGF_WARNING("sub_829815C8: estructura nula omitida (r3=0x{:08X})", ctx.r3.u32);
        ctx.r3.u64 = 0;
        return;
    }
    __imp__sub_829815C8(ctx, base);
}

extern "C" PPC_FUNC(__imp__sub_8295A518);
PPC_EXTERN_FUNC(sub_82CE4FF8); // XPhysicalFree(address)
PPC_EXTERN_FUNC(sub_82CE4C88); // VirtualFree(address, size, type)

// sub_8295A518: Free of the engine's pooled allocator (this r3, pointer r4).
PPC_FUNC(sub_8295A518)
{
    const uint32_t pointer = ctx.r4.u32;
    if (pointer >= 0xA0000000 && pointer < 0xC0000000)
    {
        constexpr uint32_t X_MEM_LARGE_PAGES = 0x20000000;
        constexpr uint32_t X_MEM_16MB_PAGES = 0x80000000;
        const uint32_t protect = MmQueryAddressProtect(pointer);
        if (protect != 0 && !(protect & (X_MEM_LARGE_PAGES | X_MEM_16MB_PAGES)))
        {
            const uint32_t self = ctx.r3.u32;
            static std::atomic<int> s_logged{ 0 };
            if (s_logged.fetch_add(1) < 4)
                LOGF_WARNING("pooled Free of a 4 KB-page physical block 0x{:08X}: back to the OS", pointer);
            ctx.r3.u64 = pointer;
            if (PPC_LOAD_U32(self + 12188) != 0)
            {
                sub_82CE4FF8(ctx, base);
            }
            else
            {
                ctx.r4.u64 = 0;
                ctx.r5.u64 = 0x8000; // MEM_RELEASE
                sub_82CE4C88(ctx, base);
            }
            return;
        }
    }
    __imp__sub_8295A518(ctx, base);
}

static std::string GuestWideString(uint8_t* base, uint32_t address, uint32_t maxLength = 200)
{
    std::string text;
    for (uint32_t i = 0; address != 0 && i < maxLength; i++)
    {
        const uint16_t c = PPC_LOAD_U16(address + 2 * i);
        if (c == 0)
            break;
        text += c < 0x80 ? char(c) : '?';
    }
    return text;
}

extern "C" PPC_FUNC(__imp__sub_8253A428);

// sub_8253A428: UGameEngine::LoadMap - ignora error de red en modo un jugador
PPC_FUNC(sub_8253A428)
{
    const uint32_t error = ctx.r6.u32;
    __imp__sub_8253A428(ctx, base);
    if (ctx.r3.u32 != 0 && error != 0 && PPC_LOAD_U32(error + 4) != 0)
    {
        static std::atomic<int> s_logged{ 0 };
        if (s_logged.fetch_add(1) < 4)
            LOGF_WARNING("map loaded, its error dropped: {}", GuestWideString(base, PPC_LOAD_U32(error)));
        PPC_STORE_U32(error + 4, 0);
    }
}
extern "C" PPC_FUNC(__imp__sub_824105B0);

namespace
{
    bool SwitchedOff(const char* name)
    {
        const char* value = std::getenv(name);
        return value != nullptr && std::strcmp(value, "0") == 0;
    }

    struct SettingValue
    {
        uint32_t offset;
        const char* key;
        uint32_t value;
    };

    constexpr SettingValue kNoShadows[] = {
        {884, "DynamicShadows", 0},
        {888, "LightEnvironmentShadows", 0},
        {932, "bEnableForegroundShadowsOnWorld", 0},
        {936, "bEnableForegroundSelfShadowing", 0},
        {940, "bAllowWholeSceneDominantShadows", 0},
    };

    constexpr SettingValue kNoEffects[] = {
        {52, "MotionBlur", 0},
        {60, "DepthOfField", 0},
        {64, "AmbientOcclusion", 0},
        {68, "Bloom", 0},
        {72, "bAllowLightShafts", 0},
        {76, "Distortion", 0},
        {92, "LensFlares", 0},
        {124, "AllowRadialBlur", 0},
    };
    constexpr uint32_t kParticleLODBias = 112;
}

// sub_824105B0: FSystemSettingsData::LoadFromIni
// Bloqueo estricto a 30 FPS con suavizado de fotogramas (Frame Smoothing) para maxima fluidez en POCO F6
PPC_FUNC(sub_824105B0)
{
    const uint32_t settings = ctx.r3.u32;
    __imp__sub_824105B0(ctx, base);

    if (settings != 0)
    {
        // bSmoothFrameRate = 1 (activa sincronizacion interna de delta-time de UE3)
        PPC_STORE_U32(settings + 44, 1);
        // MaxSmoothedFrameRate = 30.0f (IEEE 754: 0x41F00000) -> 33.33ms constantes sin tirones
        PPC_STORE_U32(settings + 48, 0x41F00000);
    }

    const bool noShadows = SwitchedOff("XPORT_SHADOWS"), noEffects = SwitchedOff("XPORT_EFFECTS");
    if (settings == 0 || (!noShadows && !noEffects))
        return;

    std::string changed;
    const auto set = [&](const SettingValue& setting)
    {
        const uint32_t old = PPC_LOAD_U32(settings + setting.offset);
        if (old == setting.value)
            return;
        PPC_STORE_U32(settings + setting.offset, setting.value);
        changed += fmt::format(" {} {}->{}", setting.key, old, setting.value);
    };
    if (noShadows)
    {
        for (const SettingValue& setting : kNoShadows)
            set(setting);
    }
    if (noEffects)
    {
        for (const SettingValue& setting : kNoEffects)
            set(setting);
        if (int32_t(PPC_LOAD_U32(settings + kParticleLODBias)) < 1)
            set({kParticleLODBias, "ParticleLODBias", 1});
    }
    static std::atomic<int> s_logged{ 0 };
    if (s_logged.fetch_add(1) < 8)
        LOGF_WARNING("system settings 0x{:08X}: 30FPS locked, shadows {}, effects {}:{}", settings,
                     noShadows ? "off" : "on", noEffects ? "off" : "on", changed.empty() ? " already so" : changed);
}

static void DropLoaderMapCheckpoints()
{
    std::error_code error;
    const std::filesystem::path folder = os::paths::GetSavePath() / "GearsCheckpoint";
    for (const auto& entry : std::filesystem::directory_iterator(folder, error))
    {
        const std::filesystem::path path = entry.path();
        if (!entry.is_regular_file(error) || path.extension() != ".sav")
            continue;
        std::string header(19, '\0');
        {
            std::ifstream file(path, std::ios::binary);
            file.read(header.data(), header.size());
            if (file.gcount() != std::streamsize(header.size()))
                continue;
        }
        const auto u8 = [&](size_t idx) -> uint32_t { return static_cast<uint8_t>(header.at(idx)); };
        const uint32_t version = (u8(0) << 24) | (u8(1) << 16) | (u8(2) << 8) | u8(3);
        const uint32_t length = (u8(4) << 24) | (u8(5) << 16) | (u8(6) << 8) | u8(7);
        static const char kLoaderMap[] = "geargame_p";
        bool loaderMap = version == 3 && length == sizeof(kLoaderMap) && u8(18) == 0;
        for (size_t i = 0; loaderMap && i + 1 < sizeof(kLoaderMap); i++)
            loaderMap = std::tolower(u8(8 + i)) == kLoaderMap[i];
        if (!loaderMap)
            continue;
        const std::filesystem::path copy =
            os::paths::GetSavePath() / (path.filename().string() + ".geargame_p.bak");
        std::filesystem::copy_file(path, copy, std::filesystem::copy_options::overwrite_existing, error);
        std::filesystem::resize_file(path, 0, error);
        LOGF_WARNING("checkpoint {} was saved in the loader map GearGame_P: emptied ({}), copy in {}",
                     path.filename().string(), error ? error.message() : "ok", copy.string());
    }
}

namespace game
{
    const char* Title()
    {
        return "Gears of War 3";
    }

    void RegisterExtraFunctions(Memory& memory)
    {
        (void)memory;
    }

    const char* ThreadName(uint32_t startAddress, uint32_t startContext, uint32_t& entry)
    {
        (void)startAddress;
        (void)startContext;
        (void)entry;
        return nullptr;
    }

    void AfterImageLoad()
    {
        DropLoaderMapCheckpoints();
    }

    const char* RendererOptions()
    {
        return "";
    }

    bool PauseMenuOpen()
    {
        return false;
    }
}

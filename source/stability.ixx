module;

#include <common.hxx>

export module stability;

import bytes;
import common;
import graphics;
import loader;
import patch;

// Crash fixes. One arm per key, each anchored to its own recovered signature.
// Registration is explicit: the driver calls Register(), so the arm cannot be
// dropped by the linker and the order the arms subscribe in is readable in one
// place.
namespace
{
    // MPNikoCrashFix: a `jz` in the player-model check becomes an unconditional
    // jump, so a model the game cannot resolve no longer falls through.
    void InstallMPNikoCrashFix()
    {
        if (!Loader::Enabled("MPNikoCrashFix"))
            return;

        auto pattern = find_pattern("74 2B 57 E8 ?? ?? ?? ?? 8B BE A8 0E 00 00");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(0), Bytes::ShortJmp, true);
    }

    // SCOSignatureFix: the instruction 32 bytes before the anchor becomes a short
    // jump, so the signature check that follows is skipped.
    void InstallSCOSignatureFix()
    {
        if (!Loader::Enabled("SCOSignatureFix"))
            return;

        auto pattern = find_pattern("8B 6C 24 1C 66 89 6E 14 64 8B 0D 2C 00 00 00 8B");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(-32), Bytes::ShortJmp, true);
    }

    // TryToSkipAllErrors: the error handler returns immediately instead of walking
    // its own path.
    void InstallTryToSkipAllErrors()
    {
        if (!Loader::Enabled("TryToSkipAllErrors"))
            return;

        auto pattern = find_pattern("81 EC 40 08 00 00 53 55 8B AC 24 4C 08 00 00");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(0), Bytes::Ret, true);
    }

    // RemoveAllWeaponsProtection: the 12-byte block the anchor jumps over is deleted,
    // so the weapon-clearing path it guarded is no longer taken.
    void InstallRemoveAllWeaponsProtection()
    {
        if (!Loader::Enabled("RemoveAllWeaponsProtection"))
            return;

        auto pattern = find_pattern("E9 ?? ?? ?? ?? 57 8D 8F ?? ?? 00 00");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(5), 12, true);
    }

    // HighFPSSpeedupFix: the nine frame-time clamps inside CTimer::Update. One
    // becomes a short jump (skipping the following test), the other eight are
    // two-byte NOPs.
    struct SpeedupSite final
    {
        ptrdiff_t offset;
        bool shortJump;
    };
    constexpr SpeedupSite kSpeedupSites[] = {
        { -7, false },
        { 3, false },
        { 224, true },
        { 231, false },
        { 367, false },
        { 377, false },
        { 607, false },
        { 614, false },
        { 764, false },
    };

    void InstallHighFPSSpeedupFix()
    {
        if (!Loader::Enabled("HighFPSSpeedupFix"))
            return;

        auto pattern = find_pattern("0F 2F C1 76 03 0F 28 C8 F3 0F 10 15");
        if (pattern.empty())
            return;

        for (const auto& site : kSpeedupSites)
        {
            if (site.shortJump)
                injector::WriteMemory<std::byte>(pattern.get_first(site.offset), Bytes::ShortJmp, true);
            else
                injector::MakeNOP(pattern.get_first(site.offset), 2, true);
        }
    }

    // AimProjectileCrashFix: the site jumps into the shared shim hosted in graphics
    // (recovered VA 0x10005590), which stores the caller's dwords and derives the
    // flag.
    void InstallAimProjectileCrashFix()
    {
        if (!Loader::Enabled("AimProjectileCrashFix"))
            return;

        auto pattern = find_pattern("8B 44 24 08 89 41 20 8B 44 24 0C 89 41 24 F7 D8");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first(0), AimProjectileShimAddress(), true);
    }
}

// Registered like every other arm: the install tier owns the gate.
export void RegisterStability()
{
    Loader::InstallEvent() += []
    {
        InstallMPNikoCrashFix();
        InstallSCOSignatureFix();
        InstallTryToSkipAllErrors();
        InstallRemoveAllWeaponsProtection();
        InstallHighFPSSpeedupFix();
        InstallAimProjectileCrashFix();
    };
}

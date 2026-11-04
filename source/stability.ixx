module;

#include <common.hxx>

export module stability;

import common;
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
        if (pattern.empty())
        {
            std::fprintf(stderr, "MPNikoCrashFix: signature not found\n");
            return;
        }

        auto* match = pattern.get(0).get<uint8_t>();
        injector::WriteMemory<uint8_t>(match, 0xEB, true);
    }

    // SCOSignatureFix: the instruction 32 bytes before the anchor becomes a short
    // jump, so the signature check that follows is skipped.
    void InstallSCOSignatureFix()
    {
        if (!Loader::Enabled("SCOSignatureFix"))
            return;

        auto pattern = find_pattern("8B 6C 24 1C 66 89 6E 14 64 8B 0D 2C 00 00 00 8B");
        if (pattern.empty())
            return;

        auto* match = pattern.get(0).get<uint8_t>();
        injector::WriteMemory<uint8_t>(match - 32, 0xEB, true);
    }

    // TryToSkipAllErrors: the error handler returns immediately instead of
    // walking its own path.
    void InstallTryToSkipAllErrors()
    {
        if (!Loader::Enabled("TryToSkipAllErrors"))
            return;

        auto pattern = find_pattern("81 EC 40 08 00 00 53 55 8B AC 24 4C 08 00 00");
        if (pattern.empty())
            return;

        auto* match = pattern.get(0).get<uint8_t>();
        injector::WriteMemory<uint8_t>(match, 0xC3, true);
    }
}

// Defined out of line in this module: the subscription body has to live in the
// module's own object, where the arm it refers to is visible.
export void RegisterStability()
{
    Loader::InstallEvent() += []
    {
        InstallMPNikoCrashFix();
        InstallSCOSignatureFix();
        InstallTryToSkipAllErrors();
    };
}

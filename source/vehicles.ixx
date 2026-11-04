module;

#include <common.hxx>

export module vehicles;

import common;
import loader;
import patch;

// Vehicle fixes. Every arm anchors to its own recovered signature; the write
// forms here are in-place bytes and tiny jump rewrites, none of them need a body.
namespace
{
    // InfiniteCarBottomDistance: the `ja` of the bottom-distance clamp becomes an
    // unconditional jump over it (`EB 2A`, written as its two bytes -- there is no
    // short-jump API).
    void InstallInfiniteCarBottomDistance()
    {
        if (!Loader::Enabled("InfiniteCarBottomDistance"))
            return;

        auto pattern = find_pattern("0F 2F D0 76 25 0F 28 C2");
        if (pattern.empty())
            return;

        auto* site = pattern.get(0).get<uint8_t>(3);
        injector::WriteMemory<uint8_t>(site, 0xEB, true);
        injector::WriteMemory<uint8_t>(site + 1, 0x2A, true);
    }

    // ReversingLightFix: the `push 0x64` eight bytes into the anchor becomes
    // `pushad`, so the reversing light path preserves the registers it clobbers.
    void InstallReversingLightFix()
    {
        if (!Loader::Enabled("ReversingLightFix"))
            return;

        auto pattern = find_pattern("11 01 00 00 8B 16 8B 42 64 8B CE FF D0 F3 0F 10");
        if (pattern.empty())
            return;

        auto* match = pattern.get(0).get<uint8_t>();
        injector::WriteMemory<uint8_t>(match + 8, 0x60, true);
    }

    // BikeFeetFix: the 12-byte rider-foot block is deleted, so the chopper foot
    // position is left to the game's own (correct) path.
    void InstallBikeFeetFix()
    {
        if (!Loader::Enabled("BikeFeetFix"))
            return;

        auto pattern = find_pattern("39 82 54 13 00 00 0F 85 94 04 00 00");
        if (pattern.empty())
            return;

        auto* match = pattern.get(0).get<uint8_t>();
        injector::MakeNOP(match + 12, 12, true);
    }

    // BikePhoneAnimsFix: the six-byte phone-animation branch is deleted.
    void InstallBikePhoneAnimsFix()
    {
        if (!Loader::Enabled("BikePhoneAnimsFix"))
            return;

        auto pattern = find_pattern("80 BE 18 02 00 00 00 0F 85 36 01 00 00 80 BE");
        if (pattern.empty())
            return;

        auto* match = pattern.get(0).get<uint8_t>();
        injector::MakeNOP(match + 33, 6, true);
    }
    // PipeBombDropIconSupport: the nine-byte check before the anchor is deleted,
    // so the pipe-bomb drop icon is accepted on the episodic builds.
    void InstallPipeBombDropIconSupport()
    {
        if (!Loader::Enabled("PipeBombDropIconSupport"))
            return;

        auto pattern = find_pattern("83 7D 18 1C 75 14");
        if (pattern.empty())
            return;

        auto* match = pattern.get(0).get<uint8_t>();
        injector::MakeNOP(match - 9, 9, true);
    }

    // TeleportProtection: the entry becomes `ret 8`. The original writes exactly
    // three bytes -- C2 08 over the opcode and 00 over the byte after the
    // immediate -- and injector::MakeRET is not equivalent: it stores the opcode
    // through a 4-byte int, which would also zero match+3.
    void InstallTeleportProtection()
    {
        if (!Loader::Enabled("TeleportProtection"))
            return;

        auto pattern = find_pattern("55 8B EC 83 E4 F8 51 53 8B 5D 08 56 57 6A 08 8D 44 24 10 8B F1 8D 7B 04 50 8B CF C7 44 24 14 00");
        if (pattern.empty())
            return;

        auto* match = pattern.get(0).get<uint8_t>();
        injector::WriteMemory<uint16_t>(injector::raw_ptr(match), 0x08C2u, true);
        injector::WriteMemory<uint8_t>(injector::raw_ptr(match + 2), 0x00u, true);
    }
    // IncreaseVehicleStructLimit: the struct count the allocator is asked for
    // becomes 100 instead of the original constant.
    void InstallIncreaseVehicleStructLimit()
    {
        if (!Loader::Enabled("IncreaseVehicleStructLimit"))
            return;

        auto pattern = find_pattern("68 ?? ?? ?? ?? 6A 32 8B C8");
        if (pattern.empty())
            return;

        auto* match = pattern.get(0).get<uint8_t>();
        injector::WriteMemory<uint8_t>(match + 6, 100, true);
    }
}

export void RegisterVehicles()
{
    Loader::InstallEvent() += []
    {
        InstallInfiniteCarBottomDistance();
        InstallReversingLightFix();
        InstallBikeFeetFix();
        InstallBikePhoneAnimsFix();
        InstallPipeBombDropIconSupport();
        InstallTeleportProtection();
        InstallIncreaseVehicleStructLimit();
    };
}

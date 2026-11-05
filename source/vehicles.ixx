module;

#include <common.hxx>

export module vehicles;

import bytes;
import common;
import loader;
import pattern;

// Vehicle fixes. Every arm anchors to its own recovered signature; the write
// forms here are in-place bytes, jump rewrites and NOP runs, none of them need a
// body.
namespace
{
    // InfiniteCarBottomDistance: the `ja` of the bottom-distance clamp becomes an
    // unconditional jump over it (`EB 2A`, written as its two bytes -- there is no
    // short-jump API).
    void InstallInfiniteCarBottomDistance()
    {
        if (!Loader::Enabled("InfiniteCarBottomDistance"))
            return;

        auto pattern = Pattern("0F 2F D0 76 25 0F 28 C2");
        if (pattern.size() > 0)
        {
            auto* site = MatchAddress(pattern, 3);
            injector::WriteMemory<std::byte>(site, Bytes::ShortJmp, true);
            injector::WriteMemory<std::byte>(site + 1, Bytes::JumpOverClamp, true);
        }
    }

    // ReversingLightFix: the `push 0x64` eight bytes into the anchor becomes
    // `pushad`, so the reversing light path preserves the registers it clobbers.
    void InstallReversingLightFix()
    {
        if (!Loader::Enabled("ReversingLightFix"))
            return;

        auto pattern = Pattern("11 01 00 00 8B 16 8B 42 64 8B CE FF D0 F3 0F 10");
        if (pattern.size() > 0)
            injector::WriteMemory<std::byte>(MatchAddress(pattern, 8), Bytes::Pushad, true);
    }

    // BikeFeetFix: the 12-byte rider-foot block is deleted, so the chopper foot
    // position is left to the game's own (correct) path.
    void InstallBikeFeetFix()
    {
        if (!Loader::Enabled("BikeFeetFix"))
            return;

        auto pattern = Pattern("39 82 54 13 00 00 0F 85 94 04 00 00");
        if (pattern.size() > 0)
            injector::MakeNOP(MatchAddress(pattern, 12), 12, true);
    }

    // BikePhoneAnimsFix: the six-byte phone-animation branch is deleted.
    void InstallBikePhoneAnimsFix()
    {
        if (!Loader::Enabled("BikePhoneAnimsFix"))
            return;

        auto pattern = Pattern("80 BE 18 02 00 00 00 0F 85 36 01 00 00 80 BE");
        if (pattern.size() > 0)
            injector::MakeNOP(MatchAddress(pattern, 33), 6, true);
    }

    // PipeBombDropIconSupport: the nine-byte check before the anchor is deleted,
    // so the pipe-bomb drop icon is accepted on the episodic builds.
    void InstallPipeBombDropIconSupport()
    {
        if (!Loader::Enabled("PipeBombDropIconSupport"))
            return;

        auto pattern = Pattern("83 7D 18 1C 75 14");
        if (pattern.size() > 0)
            injector::MakeNOP(MatchAddress(pattern, -9), 9, true);
    }

    // TeleportProtection: the entry becomes `ret 8`. The original writes exactly
    // three bytes -- C2 08 over the opcode and 00 over the byte after the
    // immediate -- and injector::MakeRET is not equivalent: it stores the opcode
    // through a 4-byte int, which would also zero match+3.
    void InstallTeleportProtection()
    {
        if (!Loader::Enabled("TeleportProtection"))
            return;

        auto pattern = Pattern("55 8B EC 83 E4 F8 51 53 8B 5D 08 56 57 6A 08 8D 44 24 10 8B F1 8D 7B 04 50 8B CF C7 44 24 14 00");
        if (pattern.size() > 0)
        {
            auto* match = MatchAddress(pattern, 0);
            injector::WriteMemory<std::byte>(injector::raw_ptr(match), Bytes::RetImm16, true);
            injector::WriteMemory<std::byte>(injector::raw_ptr(match + 2), Bytes::RetImm16High, true);
        }
    }

    // IncreaseVehicleStructLimit: the struct count the allocator is asked for
    // becomes 100 instead of the original constant.
    void InstallIncreaseVehicleStructLimit()
    {
        if (!Loader::Enabled("IncreaseVehicleStructLimit"))
            return;

        auto pattern = Pattern("68 ?? ?? ?? ?? 6A 32 8B C8");
        if (pattern.size() > 0)
            injector::WriteMemory<uint8_t>(MatchAddress(pattern, 6), 100, true);
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

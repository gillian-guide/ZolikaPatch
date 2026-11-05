module;

#include <common.hxx>

export module vehicles;

import bytes;
import common;
import loader;
import patch;

// Vehicle fixes. Every arm anchors to its own recovered signature; the write forms
// here are in-place bytes, jump rewrites and NOP runs, none of them need a body.
//
// The signatures are forged from the game database (IDA, 1.0.8.0) at each arm's
// scan anchor and are unique in that image; the offsets are measured from the
// anchor, which is why they did not change when the strings did.
namespace
{
    // InfiniteCarBottomDistance: the `ja` of the bottom-distance clamp becomes an
    // unconditional jump over it (`EB 2A`, written as its two bytes -- there is no
    // short-jump API).
    void InstallInfiniteCarBottomDistance()
    {
        if (!Loader::Enabled("InfiniteCarBottomDistance"))
            return;

        auto pattern = find_pattern("0F 2F D0 76 ? 0F 28 C2 EB ? 0F 2F CD");
        if (pattern.empty())
            return;

        auto* site = pattern.get_first<std::byte>(3);
        injector::WriteMemory<std::byte>(site, Bytes::ShortJmp, true);
        injector::WriteMemory<std::byte>(site + 1, Bytes::JumpOverClamp, true);
    }

    // ReversingLightFix: the slot displacement of `mov eax,[edx+64h]` becomes 0x60,
    // so the reversing-light path reads the neighbouring member.
    void InstallReversingLightFix()
    {
        if (!Loader::Enabled("ReversingLightFix"))
            return;

        auto pattern = find_pattern("11 01 00 00 8B 16 8B 42 64 8B CE FF D0 F3 0F 10");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(8), Bytes::Slot60, true);
    }

    // BikeFeetFix: the 12-byte rider-foot block is deleted, so the chopper foot
    // position is left to the game's own (correct) path.
    void InstallBikeFeetFix()
    {
        if (!Loader::Enabled("BikeFeetFix"))
            return;

        auto pattern = find_pattern("39 82 ? ? ? ? 0F 85 ? ? ? ? 39 05");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(12), 12, true);
    }

    // BikePhoneAnimsFix: the six-byte phone-animation branch is deleted.
    void InstallBikePhoneAnimsFix()
    {
        if (!Loader::Enabled("BikePhoneAnimsFix"))
            return;

        auto pattern = find_pattern("80 BE ? ? ? ? ? 0F 85 ? ? ? ? 80 BE ? ? ? ? ? 0F 84 ? ? ? ? 83 3D");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(33), 6, true);
    }

    // PipeBombDropIconSupport: the nine-byte check before the anchor is deleted, so
    // the pipe-bomb drop icon is accepted on the episodic builds.
    void InstallPipeBombDropIconSupport()
    {
        if (!Loader::Enabled("PipeBombDropIconSupport"))
            return;

        auto pattern = find_pattern("83 7D ? ? 75 ? 8B 15");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(-9), 9, true);
    }

    // TeleportProtection: the entry becomes `ret 8`. The original writes exactly three
    // bytes -- C2 08 over the opcode and 00 over the byte after the immediate -- and
    // injector::MakeRET is not equivalent: it stores the opcode through a 4-byte int,
    // which would also zero match+3.
    void InstallTeleportProtection()
    {
        if (!Loader::Enabled("TeleportProtection"))
            return;

        auto pattern = find_pattern("55 8B EC 83 E4 F8 51 53 8B 5D ? 56 57 6A 08");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        injector::WriteMemory<std::byte>(match, Bytes::RetImm16, true);
        injector::WriteMemory<std::byte>(match + 1, Bytes::RetImm16High, true);
        injector::WriteMemory<std::byte>(match + 2, Bytes::Zero, true);
    }

    // IncreaseVehicleStructLimit: the struct count the allocator is asked for becomes
    // 100 instead of the original constant. The signature keeps the absolute address
    // of the structure name, so it is specific to 1.0.8.0.
    void InstallIncreaseVehicleStructLimit()
    {
        if (!Loader::Enabled("IncreaseVehicleStructLimit"))
            return;

        auto pattern = find_pattern("68 34 EE DA 00");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(6), std::byte{ 100 }, true);
    }
}

// Registered like every other arm: the install tier owns the gate.
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

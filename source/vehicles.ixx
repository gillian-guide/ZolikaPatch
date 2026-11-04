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
}

export void RegisterVehicles()
{
    Loader::InstallEvent() += []
    {
        InstallInfiniteCarBottomDistance();
        InstallReversingLightFix();
    };
}

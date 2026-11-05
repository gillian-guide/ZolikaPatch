module;

#include <common.hxx>

export module stability;

import bodies;
import bytes;
import common;
import graphics;
import loader;
import patch;

// Crash fixes. One arm per key, each anchored to its own recovered signature.
// Registration is explicit: the driver calls Register(), so the arm cannot be
// dropped by the linker and the order the arms subscribe in is readable in one
// place.
//
// The signatures are forged from the game database (IDA, 1.0.8.0) at each arm's
// scan anchor and are unique in that image; the offsets below are measured from
// the anchor, which is why they did not change when the strings did.

// The published state of the arms below, in C linkage because the transcribed
// code names the slots directly (the original keeps them in its own .bss beside
// its bodies).
extern "C"
{
    uint32_t zpatch_freeze_gun_resume = 0;  // dword_1015AD64
    uint32_t zpatch_group_hack_resume = 0;  // dword_1015AD60
    uint32_t zpatch_path_node_resume_ok = 0;    // dword_1015BDFC
    uint32_t zpatch_path_node_resume_fail = 0;  // dword_1015BE00
}

// abi_path_node_shim, recovered VA 0x1003D7B0: the path node the walk just read is
// rejected when it is the 0x1FFFDF sentinel (the walk continues at the anchor) and
// otherwise carries its own `mov dl,[eax+1Fh]; test dl,1` into the resumed code,
// which is why the flags are left as the original's. Naked and internal: an
// external-linkage naked function is written to the module and every importer
// fails.
static __declspec(naked) void PathNodeShim()
{
    __asm
    {
        cmp eax, 1FFFDFh
        jz path_node_ok
        mov dl, BYTE PTR [eax + 1Fh]
        test dl, 1
        jmp DWORD PTR [zpatch_path_node_resume_fail]
    path_node_ok:
        jmp DWORD PTR [zpatch_path_node_resume_ok]
    }
}

// abi_group_hack_shim, recovered VA 0x10036E90: the group walk's first step, moved
// into the shim so the compare against the 12-byte table is made before the loop
// body. The `cmp [ecx],0` sets the flags the resume's `jnz` consumes, and the
// tail jump keeps them. Naked and internal: an external-linkage naked function is
// written to the module and every importer fails.
static __declspec(naked) void GroupHackShim()
{
    __asm
    {
        xor eax, eax
        add ecx, 4
        lea edx, [eax + 7]
        cmp ecx, 0Ch
        jz group_hack_empty
        cmp DWORD PTR [ecx], 0
        jmp DWORD PTR [zpatch_group_hack_resume]
    group_hack_empty:
        ret
    }
}

// abi_freeze_gun_shim, recovered VA 0x10036EB0: the freeze-gun path is entered
// only when the weapon's model word at +40h is a real value; the three sentinels
// (0xCDCDCDCD, -1, 0) return an empty result instead. The site is the anchor's
// -21, and the body replays the two instructions it overwrites before resuming at
// the published anchor-12 (`push edi` has already been executed by the body).
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails.
static __declspec(naked) void FreezeGunShim()
{
    __asm
    {
        mov ecx, DWORD PTR [eax + 38h]
        test ecx, ecx
        mov esi, DWORD PTR [eax + 40h]
        cmp esi, 0CDCDCDCDh
        jz freeze_gun_null
        cmp esi, 0FFFFFFFFh
        jz freeze_gun_null
        cmp esi, 0
        jz freeze_gun_null
        push edi
        jmp DWORD PTR [zpatch_freeze_gun_resume]
    freeze_gun_null:
        xor eax, eax
        pop esi
        ret 8
    }
}

namespace
{
    // MPNikoCrashFix: a `jz` in the player-model check becomes an unconditional
    // jump, so a model the game cannot resolve no longer falls through.
    void InstallMPNikoCrashFix()
    {
        if (!Loader::Enabled("MPNikoCrashFix"))
            return;

        auto pattern = find_pattern("74 ? 57 E8 ? ? ? ? 8B BE ? ? ? ? 85 FF 74 ? 8B CF E8 ? ? ? ? 57 E8 ? ? ? ? 83 C4 04 C7 86 ? ? ? ? ? ? ? ? 5F 8B 86 ? ? ? ? 85 C0 74 ? 8D 48");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(0), Bytes::ShortJmp, true);
    }

    // FreezeGunFix: the anchor's -21 is the entry of the weapon check, which is
    // replaced by the shim. The resume is the anchor's -12 (the instruction after
    // the `push edi` the body replays), published before the entry is overwritten.
    void InstallFreezeGunFix()
    {
        if (!Loader::Enabled("FreezeGunFix"))
            return;

        auto pattern = find_pattern("2B C2 D1 F8 39 3C C6");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        zpatch_freeze_gun_resume = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match) - 12);

        Bodies::Register(reinterpret_cast<uintptr_t>(&FreezeGunShim), 0x10036EB0u);
        injector::MakeJMP(match - 21, reinterpret_cast<void*>(&FreezeGunShim), true);
    }

    // GroupHackFix: the group walk's entry becomes a jump into the shim, which
    // repeats the anchor's first three instructions and the table compare. The
    // resume is the anchor's +11 (the loop's `jnz`), published before the write.
    void InstallGroupHackFix()
    {
        if (!Loader::Enabled("GroupHackFix"))
            return;

        auto pattern = find_pattern("33 C0 83 C1 04 8D 50");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        zpatch_group_hack_resume = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match) + 11);

        Bodies::Register(reinterpret_cast<uintptr_t>(&GroupHackShim), 0x10036E90u);
        injector::MakeJMP(match, reinterpret_cast<void*>(&GroupHackShim), true);
    }

    // PathNodeCrashFix: the `mov dl,[eax+1Fh]; test dl,1` at the anchor's +53 is
    // replaced by the shim, which rejects the sentinel. Both resumes are published
    // before the write: the anchor itself (the walk's next iteration) and the
    // anchor's +59 (the `jz` the original test sets the flags for).
    void InstallPathNodeCrashFix()
    {
        if (!Loader::Enabled("PathNodeCrashFix"))
            return;

        auto pattern = find_pattern("8B 04 B1 0F B7 D0");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        zpatch_path_node_resume_ok = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match));
        zpatch_path_node_resume_fail = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match) + 59);

        Bodies::Register(reinterpret_cast<uintptr_t>(&PathNodeShim), 0x1003D7B0u);
        injector::MakeJMP(match + 53, reinterpret_cast<void*>(&PathNodeShim), true);
    }

    // SCOSignatureFix: the instruction 32 bytes before the anchor becomes a short
    // jump, so the signature check that follows is skipped.
    void InstallSCOSignatureFix()
    {
        if (!Loader::Enabled("SCOSignatureFix"))
            return;

        auto pattern = find_pattern("8B 6C 24 ? 66 89 6E");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(-32), Bytes::ShortJmp, true);
    }

    // TryToSkipAllErrors: the error handler returns immediately instead of walking
    // its own path.
    void InstallTryToSkipAllErrors()
    {
        if (!Loader::Enabled("TryToSkipAllErrors"))
            return;

        auto pattern = find_pattern("81 EC 40 08 00 00");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(0), Bytes::Ret, true);
    }

    // RemoveAllWeaponsProtection: the 12-byte block the anchor jumps over is deleted,
    // so the weapon-clearing path it guarded is no longer taken.
    void InstallRemoveAllWeaponsProtection()
    {
        if (!Loader::Enabled("RemoveAllWeaponsProtection"))
            return;

        auto pattern = find_pattern("E9 ? ? ? ? 57 8D 8F");
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

        auto pattern = find_pattern("0F 2F C1 76 ? 0F 28 C8 F3 0F 10 15");
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

        auto pattern = find_pattern("8B 44 24 ? 89 41 ? 8B 44 24 ? 89 41");
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
        InstallFreezeGunFix();
        InstallGroupHackFix();
        InstallPathNodeCrashFix();
        InstallSCOSignatureFix();
        InstallTryToSkipAllErrors();
        InstallRemoveAllWeaponsProtection();
        InstallHighFPSSpeedupFix();
        InstallAimProjectileCrashFix();
    };
}

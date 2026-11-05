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
    uint32_t zpatch_plane_crash_resume_a = 0;   // dword_1015BE18
    uint32_t zpatch_plane_crash_resume_b = 0;   // dword_1015BE14
    uint32_t zpatch_blackscreen_resume = 0;     // dword_1015AD58
    uint32_t zpatch_blackscreen_value_ptr = 0;  // dword_1015AD5C
    uint32_t zpatch_crash_msg_getter = 0;       // dword_1015AD68
    uint32_t zpatch_wanted_level_resume = 0;      // dword_1015AD6C
    uint32_t zpatch_episode_switch_resume_a = 0;  // dword_10159BBC
    uint32_t zpatch_episode_switch_resume_b = 0;  // dword_10159BB4
    uint32_t zpatch_episode_switch_resume_c = 0;  // dword_10159BB8
    uint32_t zpatch_smpa60_resume_a = 0;          // dword_10159C70
    uint32_t zpatch_smpa60_resume_b = 0;          // dword_10159C68
    uint32_t zpatch_smpa60_resume_c = 0;          // dword_10159C6C
}

// tramp_SMPA60Fix, recovered VA 0x10046750: the streaming load's argument setup,
// replayed around the sentinel that says the loader slot is unused. Naked and
// internal: an external-linkage naked function is written to the module and every
// importer fails.
static __declspec(naked) void Smpa60Shim()
{
    __asm
    {
        cmp ecx, 0DDDDDDDDh
        jz smpa60_shim_tail
        mov ecx, DWORD PTR [eax + 70h]
        add eax, 40h
        push eax
        push ecx
        lea edx, [ecx + 10h]
        push edx
        push ecx
        push ecx
        jmp DWORD PTR [zpatch_smpa60_resume_b]
    smpa60_shim_tail:
        jmp DWORD PTR [zpatch_smpa60_resume_a]
    }
}

// sub_10046780, recovered VA 0x10046780: the block's link fixup, which is skipped
// when the next block is absent or is the 0xCDCDCDCD fill. Naked and internal.
static __declspec(naked) void Smpa60TailShim()
{
    __asm
    {
        mov ecx, DWORD PTR [eax + 14h]
        test ecx, ecx
        jz smpa60_tail_resume
        cmp ecx, 0CDCDCDCDh
        jz smpa60_tail_resume
        mov edx, DWORD PTR [eax + 10h]
        mov DWORD PTR [ecx + 10h], edx
    smpa60_tail_resume:
        jmp DWORD PTR [zpatch_smpa60_resume_c]
    }
}

// abi_wanted_level_shim, recovered VA 0x10050490: the wanted-level update is
// skipped when there is no player object, and the null path returns immediately.
// The body leaves the flags of its own `test al,al` for the resume's conditional.
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails.
static __declspec(naked) void WantedLevelShim()
{
    __asm
    {
        cmp ecx, 0
        jz wanted_level_null
        mov eax, DWORD PTR [ecx]
        mov edx, DWORD PTR [eax + 128h]
        call edx
        test al, al
        jmp DWORD PTR [zpatch_wanted_level_resume]
    wanted_level_null:
        pop ebx
        ret
    }
}

// abi_episode_switch_store_shim, recovered VA 0x1001B040: the episode switch's
// archive store, replayed so a failed open returns the empty result instead of
// running the tail of the function. Naked and internal.
static __declspec(naked) void EpisodeSwitchStoreShim()
{
    __asm
    {
        mov DWORD PTR [ebx + 20h], eax
        cmp eax, 0
        jz episode_switch_store_null
        mov edx, DWORD PTR [eax]
        jmp DWORD PTR [zpatch_episode_switch_resume_a]
    episode_switch_store_null:
        xor al, al
        pop edi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret 10h
    }
}

// sub_1001B060, recovered VA 0x1001B060: the reader's index check, replayed around
// a null test. Naked and internal.
static __declspec(naked) void EpisodeSwitchShimB()
{
    __asm
    {
        cmp ebx, 0
        jz episode_switch_b_null
        mov ecx, DWORD PTR [ebx + 10h]
        cmp ecx, DWORD PTR [ebx + 14h]
        jmp DWORD PTR [zpatch_episode_switch_resume_b]
    episode_switch_b_null:
        mov BYTE PTR [edi], 0
        pop edi
        pop esi
        mov eax, ebp
        pop ebp
        pop ebx
        pop ecx
        ret
    }
}

// sub_1001B0C0, recovered VA 0x1001B0C0: the same check for the caller that passes
// its object in ECX. Naked and internal.
static __declspec(naked) void EpisodeSwitchShimC()
{
    __asm
    {
        cmp ecx, 0
        jz episode_switch_c_null
        push esi
        mov esi, ecx
        cmp DWORD PTR [esi + 14h], 0
        jmp DWORD PTR [zpatch_episode_switch_resume_c]
    episode_switch_c_null:
        mov eax, 0
        ret
    }
}

// tramp_EpisodeSwitchFreezeFix_b, recovered VA 0x1001B080: the byte-stream position,
// returned as 0 when there is no stream. Naked and internal.
static __declspec(naked) void EpisodeSwitchSum()
{
    __asm
    {
        cmp ecx, 0
        jz episode_switch_sum_null
        mov eax, DWORD PTR [ecx + 10h]
        add eax, DWORD PTR [ecx + 0Ch]
        ret
    episode_switch_sum_null:
        mov eax, 0
        ret
    }
}

// sub_1001B0A0, recovered VA 0x1001B0A0: the virtual call the episode switch makes
// through the object's vtable, skipped for a null object. Naked and internal.
static __declspec(naked) void EpisodeSwitchVcall()
{
    __asm
    {
        cmp ecx, 0
        jz episode_switch_vcall_null
        mov eax, ecx
        mov ecx, DWORD PTR [eax]
        mov edx, DWORD PTR [ecx]
        mov eax, DWORD PTR [eax + 4]
        mov edx, DWORD PTR [edx + 30h]
        push eax
        call edx
        ret
    episode_switch_vcall_null:
        mov eax, 0
        ret
    }
}

// abi_plane_crash_shim, recovered VA 0x1003D870: the plane's destruction tail. The
// `mov [eax+26h],bl` at the anchor's +3 is replaced, so the body replays it after a
// null check and takes the null state's path (the anchor's -705, the head of the
// same function) instead of dereferencing a missing vehicle. Naked and internal: an
// external-linkage naked function is written to the module and every importer
// fails.
static __declspec(naked) void PlaneCrashShim()
{
    __asm
    {
        cmp eax, 0
        jz plane_crash_null
        mov BYTE PTR [eax + 26h], bl
        mov edx, DWORD PTR [esi]
        mov eax, DWORD PTR [edx + 0F4h]
        jmp DWORD PTR [zpatch_plane_crash_resume_b]
    plane_crash_null:
        jmp DWORD PTR [zpatch_plane_crash_resume_a]
    }
}

// abi_crash_msg_shim, recovered VA 0x10036F00: the message getter is called through
// the published slot, and the '~' the game uses as a format marker is turned into a
// space in the string the getter returned, in place. The `xchg ax, ax` is the
// original's loop alignment, emitted as bytes so the assembler cannot shrink it.
static __declspec(naked) void CrashMsgShim()
{
    __asm
    {
        call DWORD PTR [zpatch_crash_msg_getter]
        mov dl, BYTE PTR [eax]
        test dl, dl
        jz crash_msg_done
        mov ecx, eax
        __emit 0x66  // xchg ax, ax
        __emit 0x90
    crash_msg_loop:
        cmp dl, 7Eh
        jnz crash_msg_skip
        mov BYTE PTR [ecx], 20h
    crash_msg_skip:
        mov dl, BYTE PTR [ecx + 1]
        inc ecx
        test dl, dl
        jnz crash_msg_loop
    crash_msg_done:
        ret
    }
}

// abi_blackscreen_shim, recovered VA 0x10036EE0: the savegame-load state check.
// The arm replaces the tail `jmp` at the scan anchor's +81 with a jump here; the
// original 0x6C02A0 is then reached through the published resume, and reads the
// state dword whose address is published separately (its first instruction is
// `mov eax, ds:<dword>`, so the published word is an address, not a value).
// States 8..10 return directly and the load is skipped; every other state takes
// the original path. Naked and internal: an external-linkage naked function is
// written to the module and every importer fails.
static __declspec(naked) void BlackscreenShim()
{
    __asm
    {
        mov eax, DWORD PTR [zpatch_blackscreen_value_ptr]
        mov eax, DWORD PTR [eax]
        cmp eax, 7
        jle blackscreen_resume
        cmp eax, 0Bh
        jl blackscreen_return
    blackscreen_resume:
        jmp DWORD PTR [zpatch_blackscreen_resume]
    blackscreen_return:
        ret
    }
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

    // PlaneCrashFix: the anchor's +3 is the state store the body replays after its
    // null check. Both resumes are published first: the normal one is the anchor's
    // +14 (after the vtable read the body replays) and the null one is the anchor's
    // -705, the head of the same function.
    void InstallPlaneCrashFix()
    {
        if (!Loader::Enabled("PlaneCrashFix"))
            return;

        auto pattern = find_pattern("83 C4 04 88 58 26 8B 16 8B 82 F4 00 00 00");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        zpatch_plane_crash_resume_b = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match) + 14);
        zpatch_plane_crash_resume_a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match) - 705);

        Bodies::Register(reinterpret_cast<uintptr_t>(&PlaneCrashShim), 0x1003D870u);
        injector::MakeJMP(match + 3, reinterpret_cast<void*>(&PlaneCrashShim), true);
    }

    // BlackscreenFix: the tail jump at the anchor's +81 becomes a jump into the
    // shim. The resume is that jump's own target and the value slot is the dword
    // the resume's first instruction loads, so both are published before the write
    // (after it, the resume would be the jump itself).
    void InstallBlackscreenFix()
    {
        if (!Loader::Enabled("BlackscreenFix"))
            return;

        auto pattern = find_pattern("75 ? 8B 48 ? 8B 40 ? 51 8B 0D");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        zpatch_blackscreen_resume = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match) + 86 +
            *pattern.get(0).get<int32_t>(82));
        zpatch_blackscreen_value_ptr =
            *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(zpatch_blackscreen_resume) + 1);

        Bodies::Register(reinterpret_cast<uintptr_t>(&BlackscreenShim), 0x10036EE0u);
        injector::MakeJMP(match + 81, reinterpret_cast<void*>(&BlackscreenShim), true);
    }

    // CrashMsgFix: the thunk the anchor's -4 points at loses its branch to the
    // message getter; a call into the shim replaces it, and the shim calls the
    // getter itself, replaces the '~' format markers in place and returns to the
    // instruction after the thunk's call. Both the getter and the site are published
    // before the write.
    void InstallCrashMsgFix()
    {
        if (!Loader::Enabled("CrashMsgFix"))
            return;

        auto pattern = find_pattern("68 6A 10 4E 31");
        if (pattern.empty())
            return;

        // The pointer the anchor carries is an image VA; the offline dump loads the
        // image at another base, so it is rebased onto the image in use.
        const auto& game = Game::Get();
        const auto* headers = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            game.ImageBase() + reinterpret_cast<const IMAGE_DOS_HEADER*>(game.ImageBase())->e_lfanew);
        const auto stored = *pattern.get(0).get<uint32_t>(-4);
        auto* site = reinterpret_cast<std::byte*>(
            game.ImageBase() + (stored - headers->OptionalHeader.ImageBase));

        // The getter is the target of the branch at the thunk's entry: the
        // displacement is at +1 and is measured from +5.
        zpatch_crash_msg_getter = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(site) + 5 +
            *reinterpret_cast<const int32_t*>(site + 1));

        Bodies::Register(reinterpret_cast<uintptr_t>(&CrashMsgShim), 0x10036F00u);
        injector::MakeCALL(site, reinterpret_cast<void*>(&CrashMsgShim), true);
    }

    // WantedLevelCrashFix: the update's entry becomes the shim, which skips it for a
    // null object. The resume is the anchor's +12 (the `jz` its `test al,al` feeds).
    void InstallWantedLevelCrashFix()
    {
        if (!Loader::Enabled("WantedLevelCrashFix"))
            return;

        auto pattern = find_pattern("8B 01 8B 90 ? ? ? ? FF D2 84 C0 0F 84 ? ? ? ? 8B 83");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        zpatch_wanted_level_resume = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match) + 12);

        Bodies::Register(reinterpret_cast<uintptr_t>(&WantedLevelShim), 0x10050490u);
        injector::MakeJMP(match, reinterpret_cast<void*>(&WantedLevelShim), true);
    }

    // EpisodeSwitchFreezeFix: five sites, three resumed states. All three resumes are
    // published before the first write (they are read from the sites the writes
    // replace), and the sites are then rewritten in the original's order: the ECX
    // reader, the archive store, the stream position, the EBX reader, the virtual
    // call.
    void InstallEpisodeSwitchFreezeFix()
    {
        if (!Loader::Enabled("EpisodeSwitchFreezeFix"))
            return;

        auto pattern = find_pattern("56 8B F1 83 7E ? ? 75 ? 83 7E ? ? 74");
        if (pattern.empty())
            return;
        auto* shimC = pattern.get_first<std::byte>(0);
        zpatch_episode_switch_resume_c = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(shimC) + 7);

        pattern = find_pattern("89 43 ? 8B 10 8B 52 ? 57 8D 4C 24 ? 51 8B C8 FF D2 83 F8 FF");
        if (pattern.empty())
            return;
        auto* store = pattern.get_first<std::byte>(0);
        zpatch_episode_switch_resume_a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(store) + 5);

        pattern = find_pattern("8B 4B ? 3B 4B ? 7D");
        if (pattern.empty())
            return;
        auto* shimB = pattern.get_first<std::byte>(0);
        zpatch_episode_switch_resume_b = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(shimB) + 6);

        Bodies::Register(reinterpret_cast<uintptr_t>(&EpisodeSwitchShimC), 0x1001B0C0u);
        injector::MakeJMP(shimC, reinterpret_cast<void*>(&EpisodeSwitchShimC), true);

        Bodies::Register(reinterpret_cast<uintptr_t>(&EpisodeSwitchStoreShim), 0x1001B040u);
        injector::MakeJMP(store, reinterpret_cast<void*>(&EpisodeSwitchStoreShim), true);

        pattern = find_pattern("8B 41 10 03 41 0C C3");
        if (pattern.empty())
            return;
        Bodies::Register(reinterpret_cast<uintptr_t>(&EpisodeSwitchSum), 0x1001B080u);
        injector::MakeJMP(pattern.get_first(0), reinterpret_cast<void*>(&EpisodeSwitchSum), true);

        Bodies::Register(reinterpret_cast<uintptr_t>(&EpisodeSwitchShimB), 0x1001B060u);
        injector::MakeJMP(shimB, reinterpret_cast<void*>(&EpisodeSwitchShimB), true);

        pattern = find_pattern("8B C1 8B 08 8B 11 8B 40 04 8B 52 30 50 FF D2 C3");
        if (pattern.empty())
            return;
        Bodies::Register(reinterpret_cast<uintptr_t>(&EpisodeSwitchVcall), 0x1001B0A0u);
        injector::MakeJMP(pattern.get_first(0), reinterpret_cast<void*>(&EpisodeSwitchVcall), true);
    }

    // SMPA60Fix: two bodies and three resumed states. The tail shim's resume is the
    // primary anchor's +136 and the `jz` at the anchor's -38 becomes an unconditional
    // short jump; the second anchor is the streaming loop's loader call, whose
    // sentinel path and normal path use the other two resumes. Everything is
    // published before the first write.
    void InstallSMPA60Fix()
    {
        if (!Loader::Enabled("SMPA60Fix"))
            return;

        auto pattern = find_pattern("01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C");
        if (pattern.empty())
            return;

        auto* primary = pattern.get_first<std::byte>(0);
        zpatch_smpa60_resume_c = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(primary) + 136);

        pattern = find_pattern("8B 48 70 83 C0 40 50 51 8D 51 10");
        if (pattern.empty())
            return;

        auto* shim = pattern.get_first<std::byte>(0);
        zpatch_smpa60_resume_a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(shim) + 28);
        zpatch_smpa60_resume_b = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(shim) + 14);

        Bodies::Register(reinterpret_cast<uintptr_t>(&Smpa60TailShim), 0x10046780u);
        injector::MakeJMP(primary + 123, reinterpret_cast<void*>(&Smpa60TailShim), true);
        injector::WriteMemory<std::byte>(primary - 38, Bytes::ShortJmp, true);

        Bodies::Register(reinterpret_cast<uintptr_t>(&Smpa60Shim), 0x10046750u);
        injector::MakeJMP(shim, reinterpret_cast<void*>(&Smpa60Shim), true);
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
        InstallBlackscreenFix();
        InstallCrashMsgFix();
        InstallFreezeGunFix();
        InstallGroupHackFix();
        InstallPathNodeCrashFix();
        InstallPlaneCrashFix();
        InstallWantedLevelCrashFix();
        InstallEpisodeSwitchFreezeFix();
        InstallSMPA60Fix();
        InstallSCOSignatureFix();
        InstallTryToSkipAllErrors();
        InstallRemoveAllWeaponsProtection();
        InstallHighFPSSpeedupFix();
        InstallAimProjectileCrashFix();
    };
}

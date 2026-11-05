module;

#include <common.hxx>

// Declares the /GS cookie the transcribed body's prologue reads and the check its
// epilogue calls (`__security_cookie`, `__security_check_cookie` in vcruntime.h).
#include <intrin.h>

export module audio;

import bodies;
import bytes;
import common;
import game;
import loader;
import patch;
import shared;

// The fixes the original groups with the radio and the world: the radio freeze,
// the episode gates the weapons and the helicopter ceiling sit behind, the input
// behaviour (mouse, minimize), the pointer-node pool sizes, the MP sync, the
// physics sleep, the model-hash hook and the cutscene player-model check. One arm
// per key, each anchored to its own recovered signature.
//
// The signatures are forged from the game database (IDA, 1.0.8.0) at each arm's
// scan anchor and are unique in that image; the offsets below are measured from
// the anchor, which is why they did not change when the strings did.

// The published state of the arms below, in C linkage because the transcribed
// code names the slots directly (the original keeps them in its own .bss beside
// its bodies).
extern "C"
{
    // dword_10159C0C, the game's model-index lookup shared.ixx publishes: the
    // PedModels body reads it through the same slot (the C name resolves across
    // module units; a second definition would be a second scan).
    extern uintptr_t zpatch_model_lookup;

    // The shared resolver's address. The PedModels body has to reach sub_10003E50
    // with the original's direct `call`, and that body is internal to shared.ixx
    // (a naked function with external linkage in a module interface unit fails
    // every importer, C2488), so the call lands on a thunk that tail-jumps here.
    uintptr_t zpatch_audio_value_resolver = 0;

    // The "player" model name the PedModels body pushes (original 0x1013A038). A
    // copy rather than shared.ixx's literal: a module-attached literal is not
    // visible to an importer, and the proof compares the push as a slot anyway.
    const char zpatch_audio_player_name[] = "player";
}


// sub_10005ED0, recovered VA 0x10005ED0 (6 bytes, 2 instructions): the radio
// freeze value. The site is a call into the game's radio update; the body returns
// 19 in its place. Naked and internal: an external-linkage naked function is
// written to the module and every importer fails (C2488).
static __declspec(naked) void CombinedRadioFreezeFixBody()
{
    __asm
    {
        mov eax, 13h
        ret
    }
}

// sub_100057F0, recovered VA 0x100057F0 (3 bytes, 2 instructions): the MP sync
// value. The site is the virtual call the sync path makes; the body answers it
// with 0 instead. Naked and internal.
static __declspec(naked) void BetterMPSyncBody()
{
    __asm
    {
        xor eax, eax
        ret
    }
}

// A three-instruction thunk onto the shared resolver: `jmp` through the published
// slot pushes nothing, so the resolver sees the same stack and flags the
// original's `call sub_10003E50` gives it, and its `ret` returns to the body.
static __declspec(naked) void ValueResolverThunk()
{
    __asm
    {
        jmp DWORD PTR [zpatch_audio_value_resolver]
    }
}

// sub_10050870, recovered VA 0x10050870 (75 bytes, 26 instructions): the "does
// this object use the player model" check the cutscene code calls. It resolves
// "player" through the shared resolver, asks the game's model lookup for that
// model's index and compares it with the object's model word (+2Eh). The /GS
// prologue and epilogue are the original's, including the CRT check call.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void PedModelsInCutscenesCheckBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 8
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        push esi
        push OFFSET zpatch_audio_player_name
        mov esi, ecx
        call ValueResolverThunk
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        movsx eax, WORD PTR [esi + 2Eh]
        add esp, 0Ch
        cmp eax, DWORD PTR [ebp - 8]
        mov ecx, DWORD PTR [ebp - 4]
        setz al
        xor ecx, ebp
        pop esi
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

namespace
{
    // AllowHashesForIDE: the game's model-name hash function becomes the shared
    // resolver (sub_10003E50), so an IDE entry may name a model by `hash:NNNN`.
    // The game calls the hash function with two arguments; the resolver is
    // __cdecl and reads only the first, so its plain `ret` leaves the second for
    // the caller to pop, exactly as the original's body does.
    void InstallAllowHashesForIDE()
    {
        if (!Loader::Enabled("AllowHashesForIDE"))
            return;

        auto pattern = find_pattern("8B 4C 24 08 56 8B 74 24 08 80 3E 22");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first(0),
                reinterpret_cast<void*>(Shared::ValueResolver()), true);
    }

    // CombinedRadioFreezeFix: the call at the anchor is the radio update; it
    // becomes a call into the body, which returns the freeze value instead of
    // running the update.
    void InstallCombinedRadioFreezeFix()
    {
        if (!Loader::Enabled("CombinedRadioFreezeFix"))
            return;

        auto pattern = find_pattern("E8 ? ? ? ? 8B 35 ? ? ? ? 3B F0");
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&CombinedRadioFreezeFixBody), 0x10005ED0u);
            injector::MakeCALL(pattern.get_first(0),
                reinterpret_cast<void*>(&CombinedRadioFreezeFixBody), true);
        }
    }

    // BetterMPSync: the sync path's virtual call is the five bytes at the anchor's
    // +6 (`push ebp; mov ecx,esi; call edx`); the call is replaced by a call into
    // the body, which answers 0 and leaves the stack balanced (the argument push
    // and the callee's own pop are both gone).
    void InstallBetterMPSync()
    {
        if (!Loader::Enabled("BetterMPSync"))
            return;

        auto pattern = find_pattern("8B 90 E8 00 00 00 55 8B CE FF");
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&BetterMPSyncBody), 0x100057F0u);
            injector::MakeCALL(pattern.get_first(6),
                reinterpret_cast<void*>(&BetterMPSyncBody), true);
        }
    }

    // IncreasePtrNodes: the three atPool sizes the pointer-node lists allocate.
    // Each site is `push <flags>; push <name>; push <count>`, so the arm rewrites
    // the count at the anchor's +8: 80,000 -> 320,000 for the single-node pool and
    // 16,000 -> 64,000 for the two double-node pools.
    void InstallIncreasePtrNodes()
    {
        if (!Loader::Enabled("IncreasePtrNodes"))
            return;

        auto pattern = find_pattern("6A 08 ? ? ? ? ? 68 80 38 01 00");
        if (!pattern.empty())
            injector::WriteMemory<uint32_t>(pattern.get_first(8), 320'000u, true);

        pattern = find_pattern("6A 10 ? ? ? ? ? 68 80 3E 00 00");
        if (!pattern.empty())
            injector::WriteMemory<uint32_t>(pattern.get_first(8), 64'000u, true);

        pattern = find_pattern("6A 14 ? ? ? ? ? 68 80 3E 00 00");
        if (!pattern.empty())
            injector::WriteMemory<uint32_t>(pattern.get_first(8), 64'000u, true);
    }

    // NoPhysicsSleep: the physics sleep threshold is the float the anchor's -38
    // operand names (0.005f at game 0x1090B18, RVA 0xC90B18), so the arm writes a
    // zero there. The operand is an absolute game VA and the image may be mapped
    // elsewhere (the offline dump is), so it is rebased onto the image in use
    // first.
    void InstallNoPhysicsSleep()
    {
        if (!Loader::Enabled("NoPhysicsSleep"))
            return;

        auto pattern = find_pattern("0F 00 00 00 C7 41 14 14 00 00 00 C7 41 1C 00 00");
        if (pattern.empty())
            return;

        const auto& game = Game::Get();
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game.ImageBase());
        const auto* headers =
            reinterpret_cast<const IMAGE_NT_HEADERS*>(game.ImageBase() + dos->e_lfanew);
        const auto threshold = *pattern.get(0).get<uint32_t>(-38);
        injector::WriteMemory<uint32_t>(
            game.ImageBase() + (threshold - headers->OptionalHeader.ImageBase), 0u, true);
    }

    // The game waits ten ticks when the window is minimized; the original writes
    // six, so the arm does too.
    constexpr std::byte kMinimizePauseTick{ 0x06 };

    // DoNotPauseOnMinimize: the wait's tick count drops to six, and the branch
    // that pauses the game when the window loses focus is NOPed.
    void InstallDoNotPauseOnMinimize()
    {
        if (!Loader::Enabled("DoNotPauseOnMinimize"))
            return;

        auto pattern = find_pattern("6A 0A 56 50 FF D2 A1");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(1), kMinimizePauseTick, true);

        pattern = find_pattern("75 ? 8B 0D ? ? ? ? 51 FF 15 ? ? ? ? 85 C0 75 ? 8B 15");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 2, true);
    }

    // One site of the episode gate: the recovered signature, the offset from its
    // anchor the NOP starts at, and how many bytes it covers. A row NOPs either
    // the comparison itself or the branch after the six-byte `cmp [episode],2`.
    struct EpisodeGateSite final
    {
        std::string_view signature;
        ptrdiff_t offset;
        size_t length;
    };

    // EpisodicWeaponSupport: the eight episode gates in front of the episodic
    // weapons. The key's build mask is the original's ten-build set (options.inc),
    // so the arm needs no gate of its own.
    constexpr EpisodeGateSite kEpisodicWeaponSites[] = {
        { "75 5B 83 3D ? ? ? ? 00 75", 0, 2 },
        { "83 3D ? ? ? ? 02 0F 85 8C 01 00 00", 7, 6 },
        { "83 3D ? ? ? ? 02 0F 8C D5 00", 0, 13 },
        { "83 3D ? ? ? ? 02 75 20 8B CD E8", 7, 2 },
        { "83 3D ? ? ? ? 02 75 50", 7, 2 },
        { "83 3D ? ? ? ? 02 7C 21 8B 44 24 0C", 7, 2 },
        { "83 3D ? ? ? ? 02 7C 32 8B 41", 7, 2 },
        { "83 3D ? ? ? ? 02 7C 7D 8B 4C 24 0C", 7, 2 },
    };

    void InstallEpisodicWeaponSupport()
    {
        if (!Loader::Enabled("EpisodicWeaponSupport"))
            return;

        for (const auto& site : kEpisodicWeaponSites)
        {
            auto pattern = find_pattern(site.signature);
            if (!pattern.empty())
                injector::MakeNOP(pattern.get_first(site.offset), site.length, true);
        }
    }

    // PoliceEpisodicWeaponSupport: the police-vehicle episode gate; the two-byte
    // branch after the `cmp [episode],2` at the anchor is NOPed.
    void InstallPoliceEpisodicWeaponSupport()
    {
        if (!Loader::Enabled("PoliceEpisodicWeaponSupport"))
            return;

        auto pattern = find_pattern("39 2D ? ? ? ? 75 3E");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(6), 2, true);
    }

    // TBoGTHeliHeightLimit: the helicopter ceiling's episode gate -- the branch
    // after the `cmp [episode],2` at the anchor is NOPed, so the ceiling applies
    // on every episode.
    void InstallTBoGTHeliHeightLimit()
    {
        if (!Loader::Enabled("TBoGTHeliHeightLimit"))
            return;

        auto pattern = find_pattern("83 3D ? ? ? ? 02 7C 0A F3 0F");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(7), 2, true);
    }

    // PedModelsInCutscenesFix: the cutscene code's player-model check becomes the
    // body, which re-derives the player model's index through the shared resolver
    // and the game lookup and compares that with the object's model word.
    void InstallPedModelsInCutscenesFix()
    {
        if (!Loader::Enabled("PedModelsInCutscenesFix"))
            return;

        auto pattern = find_pattern("56 8B F1 E8 ? ? ? ? 85 C0 74 0E 66 8B 40 2E");
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&PedModelsInCutscenesCheckBody),
                0x10050870u);
            injector::MakeJMP(pattern.get_first(0),
                reinterpret_cast<void*>(&PedModelsInCutscenesCheckBody), true);
        }
    }
}

// Three of this module's keys are deliberately not installed:
//
//   * RestoreDeathMusic: its two bodies (sub_10050A50, 41 instructions, and
//     sub_10050AD0, 91) answer through the original's script-native layer
//     (sub_1005B3F0/sub_1005B150 and the 0xA0-byte native context), which this
//     tree does not have, so the three calls they answer cannot be transcribed.
//   * MouseFix: six bodies plus the 62-instruction FPU helper sub_10024440;
//     sub_100241E0 alone is 138 instructions, and the arm writes 25 places.
//   * OutOfCommissionFix: the key's payload is the original's embedded, compressed
//     finale2.sco (163,796 bytes at 0x100B6798), decoded and registered by
//     sub_1004C3F0 (16,311 bytes); its 7-instruction branch body alone would widen
//     a branch the unregistered table never feeds.
//
// Each stays open rather than being stubbed.

// Registered like every other arm: the install tier owns the gate.
export void RegisterAudio()
{
    // The thunk's slot: published once, before any arm can install the body.
    zpatch_audio_value_resolver = Shared::ValueResolver();

    Loader::InstallEvent() += []
    {
        InstallAllowHashesForIDE();
        InstallCombinedRadioFreezeFix();
        InstallBetterMPSync();
        InstallIncreasePtrNodes();
        InstallNoPhysicsSleep();
        InstallDoNotPauseOnMinimize();
        InstallEpisodicWeaponSupport();
        InstallPoliceEpisodicWeaponSupport();
        InstallTBoGTHeliHeightLimit();
        InstallPedModelsInCutscenesFix();
    };
}

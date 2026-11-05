module;

#include <common.hxx>
#include <cstring>

export module loading;

import bodies;
import bytes;
import common;
import config;
import game;
import loader;
import patch;

// The load-time arms: the fixes the original applies while the game is loading,
// one arm per INI key, each anchored to its own recovered signature.
namespace
{
    // FastLoading: the ten-millisecond sleep the streaming code takes between
    // batches. The arm zeroes the pushed `sleep(10)` argument; the original handler
    // writes the one byte at the anchor's +1.
    void InstallFastLoading()
    {
        if (!Loader::Enabled("FastLoading"))
            return;

        auto pattern = find_pattern("6A 0A E8 ? ? ? ? 83 C7 0A 83 C4 04 81 FF E8 03 00 00 75 02 33 FF");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first<std::byte>(1), Bytes::Zero, true);
    }

    // NoLoadingSleep: the loading screen's five-second `Sleep(5000)` becomes
    // `Sleep(0)`. The arm zeroes the pushed immediate at the anchor's +1.
    void InstallNoLoadingSleep()
    {
        if (!Loader::Enabled("NoLoadingSleep"))
            return;

        auto pattern = find_pattern("68 88 13 00 00 FF 15");
        if (!pattern.empty())
            injector::WriteMemory<uint32_t>(pattern.get_first<uint32_t>(1), 0u, true);
    }
}

// RemoveGFWLUpdatePopup, as the original handler sub_10005500 applies it: the
// update window's date arithmetic at game 0x7CBF70 is replaced by a stub that
// first disables the four xlive.dll update-window flag slots, then replays the
// four instructions the jump overwrites and resumes at the anchor's +12.
// The published state of the arms below, in C linkage because the transcribed
// code names the slots directly (the original keeps them in its own .bss beside
// its bodies).
extern "C"
{
    uint32_t zpatch_gfwl_resume = 0;  // dword_10159C18
    uint32_t zpatch_xlive_base = 0;   // dword_10159C10
}

// sub_10005340's RemoveGFWLUpdatePopup branch: store the "update disabled" dword
// into the four xlive.dll flag slots. `zpatch_xlive_base` is the module the
// loader captured; when xlive.dll is absent it is null and the write lands on the
// low 0x66F770 page, which is the original's fault (preserved, not guarded).
static void GfwlPatch()
{
    for (const uint32_t offset : { 0x66F770u, 0x66F774u, 0x6B55D0u, 0x6B55F0u })
        injector::WriteMemory<uint32_t>(zpatch_xlive_base + offset, 0x10000E83u, true);
}

// sub_100054E0, recovered VA 0x100054E0 (8 instructions): the shared
// Patch4RemoveModChecks / RemoveGFWLUpdatePopup stub. It runs the flag-gated
// helper between pusha/popa, replays the 64-bit ECX:ESI subtraction from EDX:EAX
// the site held (and the two stores of its result into the caller's argument
// slots), then tail-jumps to the replayed site's continuation. `pusha`/`popa` are
// emitted as bytes: the inline assembler renders both with a 0x66 operand-size
// prefix, which the original does not have.
static __declspec(naked) void GfwlStubBody()
{
    __asm
    {
        __emit 0x60  // pusha
        call GfwlPatch
        __emit 0x61  // popa
        sub eax, ecx
        sbb edx, esi
        mov DWORD PTR [esp + 0Ch], eax
        mov DWORD PTR [esp + 10h], edx
        jmp DWORD PTR [zpatch_gfwl_resume]
    }
}

namespace
{
    // RemoveGFWLUpdatePopup: the anchor's first twelve bytes are the subtraction
    // and its two stores; the stub replays them and resumes at match + 12.
    void InstallRemoveGFWLUpdatePopup()
    {
        if (!Loader::Enabled("RemoveGFWLUpdatePopup"))
            return;

        // dword_10159C10: the xlive.dll module the original's loader captures.
        zpatch_xlive_base =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GetModuleHandleA("xlive.dll")));

        auto pattern = find_pattern("2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            zpatch_gfwl_resume = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match) + 12);
            injector::MakeJMP(match, reinterpret_cast<void*>(&GfwlStubBody), true);
        }
    }
}

// sub_10038550, recovered VA 0x10038550 (2 instructions): the RGSC login check's
// replacement. It sets only AL, so the caller sees "already signed in" and the
// upper 24 bits of EAX stay whatever the caller had, exactly as the original.
static __declspec(naked) void RgscLoginRemovedBody()
{
    __asm
    {
        mov al, 1
        ret
    }
}

namespace
{
    // RemoveRGSCLoginRequirement: the login-required branch of the RGSC launch
    // path. On 1.0.8.0 the original writes the same five-byte jump twice (the
    // duplicate is kept); the second anchor never matches on that build but does
    // on the ones the key was gated for.
    void InstallRemoveRGSCLoginRequirement()
    {
        if (!Loader::Enabled("RemoveRGSCLoginRequirement"))
            return;

        auto pattern = find_pattern("8B 44 24 ? 85 C0 74 ? F3 0F 7E 05 ? ? ? ? 66 0F D6 00");
        if (Loader::Current() == Build::v1080 && !pattern.empty())
        {
            auto* site = pattern.get_first<std::byte>(0);  // 0x498460
            injector::MakeJMP(site, reinterpret_cast<void*>(&RgscLoginRemovedBody), true);
            injector::MakeJMP(site, reinterpret_cast<void*>(&RgscLoginRemovedBody), true);
        }

        pattern = find_pattern("83 EC 44 A1 ? ? ? ? 33 C4 89 44 24 40 53 8B 5C 24 4C 56 68 ? ? ? ? E8 ? ? ? ? 83 C4");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&RgscLoginRemovedBody), true);
    }
}

namespace
{
    // StartupTimeFix: the game's three startup-timer sites each `push` a dword
    // pointer; the original retargets every one of them (its handler collects all
    // matches) to its own timing word at the anchor's +6. The word itself is zero.
    uint32_t zpatch_startup_time = 0;

    void InstallStartupTimeFix()
    {
        if (!Loader::Enabled("StartupTimeFix"))
            return;

        // Three sites on 1.0.8.0: the scan hint has to ask for all of them (the
        // scanner stops at the hint).
        auto pattern = find_pattern<3>("68 00 C2 EC 80 68");
        if (!pattern.empty())
        {
            for (size_t index = 0; index < pattern.size(); ++index)
            {
                injector::WriteMemory<uint32_t>(pattern.get(index).get<std::byte>(6),
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_startup_time)), true);
                Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(pattern.get(index).get<std::byte>(6)));
            }
        }
    }
}

// The published state of the two build-gated arms below, in C linkage because the
// transcribed code names the slots directly.
extern "C"
{
    uint32_t zpatch_benchmark_resume_a = 0;  // dword_10159CC4
    uint32_t zpatch_benchmark_resume_b = 0;  // dword_10159CC8
}

// sub_10005650, recovered VA 0x10005650: BenchmarkFix's first match-site shim. It
// checks the benchmark object, runs the interposed callback (ESI with EDI), then
// resumes, or unwinds through the enclosing function's tail when the object is
// null.
static __declspec(naked) void BenchmarkBodyA()
{
    __asm
    {
        test eax, eax
        jz benchmark_a_zero
        push edi
        mov ebx, eax
        call esi
        jmp DWORD PTR [zpatch_benchmark_resume_a]
    benchmark_a_zero:
        xor al, al
        pop esi
        pop ebp
        pop ebx
        add esp, 10h
        ret
    }
}

// sub_10005670, recovered VA 0x10005670: BenchmarkFix's second match-site shim,
// the same shape as the first with its own resume.
static __declspec(naked) void BenchmarkBodyB()
{
    __asm
    {
        test eax, eax
        jz benchmark_b_zero
        push edi
        mov ebx, eax
        call esi
        jmp DWORD PTR [zpatch_benchmark_resume_b]
    benchmark_b_zero:
        xor al, al
        pop esi
        pop ebp
        pop ebx
        add esp, 10h
        ret
    }
}

namespace
{
    // VSyncFix: both call shapes load the vsync flag from an operand the original
    // retargets to its own dword. The key's build mask excludes 1.0.8.0, where
    // neither anchor exists, so the arm is inert on that image.
    uint32_t zpatch_vsync_flag = 0;

    void InstallVSyncFix()
    {
        if (!Loader::Enabled("VSyncFix"))
            return;

        auto pattern = find_pattern("8B 08 8B 51 0C 6A 01 6A 00 6A 00 6A 00 6A 00 50 FF D2 8B F0 81 FE ? ? ? ? 74");
        if (!pattern.empty())
        {
            injector::WriteMemory<uint32_t>(pattern.get_first<std::byte>(30),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_vsync_flag)), true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(30)));
        }

        pattern = find_pattern("8B 10 50 8B 82 A8 00 00 00 FF D0 83");
        if (!pattern.empty())
        {
            injector::WriteMemory<uint32_t>(pattern.get_first<std::byte>(13),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_vsync_flag)), true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(13)));
        }
    }

    // BenchmarkFix: two match sites eight bytes into the matched call sequence.
    // The resume is the anchor's +13, read before the sites are overwritten; the
    // key's build mask excludes 1.0.8.0, so this arm is inert on that image.
    void InstallBenchmarkFix()
    {
        if (!Loader::Enabled("BenchmarkFix"))
            return;

        auto pattern = find_pattern<2>("E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20");
        if (!pattern.empty())
        {
            const auto resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get(0).get<std::byte>(0)) + 13);
            zpatch_benchmark_resume_a = resume;
            zpatch_benchmark_resume_b = resume;

            injector::MakeJMP(pattern.get(0).get<std::byte>(8), reinterpret_cast<void*>(&BenchmarkBodyA), true);
            if (pattern.size() > 1)
                injector::MakeJMP(pattern.get(1).get<std::byte>(8), reinterpret_cast<void*>(&BenchmarkBodyB), true);
        }
    }
}

namespace
{
    // OverrideGTADatPath's sibling key: the path the game's gta.dat push is
    // retargeted to. The original's buffer is 64 bytes and its static initializer
    // holds the stock path; the loader overwrites it from OverrideGTADatPathTo
    // with strcpy_s, so a value of 64 bytes or more faults through the CRT's
    // invalid-parameter path (preserved). Absent or "0" leaves the initializer.
    char zpatch_dat_path[64] = "common:/data/gta.dat";

    void CopyOverrideDatPath()
    {
        const std::string fallback("0");
        const auto& value = Feather::Value(Config::Ini(), "Options", "OverrideGTADatPathTo", fallback);
        if (value == "0")
            return;

        strcpy_s(zpatch_dat_path, sizeof(zpatch_dat_path), value.c_str());
    }

    // OverrideGTADatPath: the anchor is the gta.dat loader's `push <char*>`, and
    // the original rewrites just that immediate (the anchor's +1).
    void InstallOverrideGTADatPath()
    {
        if (!Loader::Enabled("OverrideGTADatPath"))
            return;

        auto pattern = find_pattern("68 ? ? ? ? E8 ? ? ? ? 83 C4 04 E8 ? ? ? ? E8 ? ? ? ? E8");
        if (!pattern.empty())
        {
            injector::WriteMemory<uint32_t>(pattern.get_first<std::byte>(1),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(zpatch_dat_path)), true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(1)));
        }
    }
}

// The original's FusionFix probe (sub_10003E20): a loaded
// GTAIV.EFLC.FusionFix.asi whose two signature dwords do not match the known
// build closes SkipIntro/SkipMenu (FusionFix owns those fixes there). A loaded
// module probes the dwords unconditionally, as the original does.
static bool FusionFixGateClosed()
{
    const HMODULE module = GetModuleHandleW(L"GTAIV.EFLC.FusionFix.asi");
    if (module == nullptr)
        return false;

    const auto* image = reinterpret_cast<const std::byte*>(module);
    uint32_t first{};
    uint32_t second{};
    std::memcpy(&first, image + 0x20D3, sizeof(first));
    std::memcpy(&second, image + 0x20D7, sizeof(second));
    return first != 0xC7C47589u || second != 0x00002446u;
}

extern "C"
{
    uint32_t zpatch_skip_intro_match = 0;    // dword_1015AD74
    uint32_t zpatch_skip_menu_resume = 0;    // dword_10159348
    uint8_t zpatch_skip_menu_flag = 0;       // byte_101A8450
}

// sub_10045C30, recovered VA 0x10045C30 (6 instructions): the intro-screen
// detour. Entered through a jump from the anchor's -6 with the site's ECX and
// EDX; it scales ECX by 400 and zeroes EDX below the 8000 threshold, which is
// what skips the intro outright, then resumes at the anchor.
static __declspec(naked) void SkipIntroBody()
{
    __asm
    {
        imul ecx, ecx, 190h
        cmp edx, 1F40h
        jge skip_intro_ok
        xor edx, edx
    skip_intro_ok:
        jmp DWORD PTR [zpatch_skip_intro_match]
    }
}

// sub_10046610, recovered VA 0x10046610 (12 instructions): the menu detour. The
// site's call is retargeted to this body, whose first call with a zero argument
// switches the menu id to 6 (one-shot through the flag), then tail-jumps to the
// original call's target; the call's own return address stays on the stack, so
// the target returns to the game's next instruction.
static __declspec(naked) void SkipMenuBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        cmp BYTE PTR [zpatch_skip_menu_flag], 0
        mov eax, DWORD PTR [ebp + 8]
        jne skip_menu_go
        test eax, eax
        jne skip_menu_go
        mov eax, 6
        mov BYTE PTR [zpatch_skip_menu_flag], 1
    skip_menu_go:
        mov DWORD PTR [ebp + 8], eax
        pop ebp
        jmp DWORD PTR [zpatch_skip_menu_resume]
    }
}

namespace
{
    // SkipIntro: the anchor is the intro sequence's store; the arm jumps from the
    // anchor's -6, and the body resumes at the anchor itself.
    void InstallSkipIntro()
    {
        if (!Loader::Enabled("SkipIntro") || FusionFixGateClosed())
            return;

        auto pattern = find_pattern("89 91 ? ? ? ? 8D 44 24 ? 68 ? ? ? ? 50");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            zpatch_skip_intro_match = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match));
            injector::MakeJMP(match - 6, reinterpret_cast<void*>(&SkipIntroBody), true);
        }
    }

    // SkipMenu: each anchor's call is retargeted to the body, and the resume is
    // the call's original target (the call's displacement is read before the
    // site is overwritten).
    void InstallSkipMenu()
    {
        if (!Loader::Enabled("SkipMenu") || FusionFixGateClosed())
            return;

        auto pattern = find_pattern("83 F8 03 75 ? A1 ? ? ? ? 80 88 ? ? ? ? ? 84 DB 74 0A 6A 00 E8 ? ? ? ? 83 C4 04 5F 5E");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            int32_t displacement{};
            std::memcpy(&displacement, match + 24, sizeof(displacement));
            zpatch_skip_menu_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(match) + 28 + displacement);
            injector::MakeCALL(match + 23, reinterpret_cast<void*>(&SkipMenuBody), true);
        }

        pattern = find_pattern("6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5D 5B 33 CC E8 ? ? ? ? 81 C4 ? ? ? ? C3");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            int32_t displacement{};
            std::memcpy(&displacement, match + 3, sizeof(displacement));
            zpatch_skip_menu_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(match) + 7 + displacement);
            injector::MakeCALL(match + 2, reinterpret_cast<void*>(&SkipMenuBody), true);
        }

        pattern = find_pattern("6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5B 33 CC E8 ? ? ? ? 8B E5 5D C3");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            int32_t displacement{};
            std::memcpy(&displacement, match + 3, sizeof(displacement));
            zpatch_skip_menu_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(match) + 7 + displacement);
            injector::MakeCALL(match + 2, reinterpret_cast<void*>(&SkipMenuBody), true);
        }
    }
}

// SavegameFix's four replacement functions, transcribed from the original's
// bodies. They replace game functions the save pipeline calls; each keeps the
// original's __stdcall argument cleanup.
extern "C"
{
    uintptr_t zpatch_savegame_close_handle = 0;   // &CloseHandle
    uintptr_t zpatch_savegame_create_mutex = 0;   // &CreateMutexA
}

// sub_10038570, recovered VA 0x10038570 (2 instructions): the "no savegame"
// path returns 0.
static __declspec(naked) void SavegameNullBody()
{
    __asm
    {
        xor eax, eax
        ret 8
    }
}

// sub_10045580, recovered VA 0x10045580 (7 instructions): close the save
// handle and report success.
static __declspec(naked) void SavegameCloseBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        push DWORD PTR [ebp + 8]
        call DWORD PTR [zpatch_savegame_close_handle]
        xor eax, eax
        pop ebp
        ret 4
    }
}

// sub_100455E0, recovered VA 0x100455E0 (19 instructions): create the save
// mutex into the caller's handle slot; a null slot reports 1.
static __declspec(naked) void SavegameMutexBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        push esi
        mov esi, DWORD PTR [ebp + 0Ch]
        test esi, esi
        jz savegame_mutex_null
        push 0
        push 0
        push 0
        call DWORD PTR [zpatch_savegame_create_mutex]
        mov DWORD PTR [esi], eax
        xor eax, eax
        pop esi
        pop ebp
        ret 8
    savegame_mutex_null:
        mov eax, 1
        pop esi
        pop ebp
        ret 8
    }
}

// The inline assembler refuses `call memcpy` (it is a compiler intrinsic), so the
// copy path calls this thin wrapper instead; the original calls the CRT directly.
// The call shape is the same (`call imm`), the caller cleans the three arguments,
// and the copy itself is the CRT's.
static void __cdecl SavegameCopyBlock(void* destination, const void* source, std::size_t size)
{
    std::memcpy(destination, source, size);
}

// sub_100455A0, recovered VA 0x100455A0 (28 instructions): the read/copy path.
// It refuses when the destination buffer is smaller than the requested size
// (0x8007007A), otherwise copies through the CRT and records the size.
static __declspec(naked) void SavegameCopyBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        push esi
        mov esi, DWORD PTR [ebp + 0Ch]
        push edi
        mov edi, DWORD PTR [ebp + 14h]
        cmp DWORD PTR [edi], esi
        jnb savegame_copy_room
        mov DWORD PTR [edi], esi
        mov eax, 8007007Ah
        pop edi
        pop esi
        pop ebp
        ret 14h
    savegame_copy_room:
        mov eax, DWORD PTR [ebp + 10h]
        test eax, eax
        jz savegame_copy_skip
        push esi
        push DWORD PTR [ebp + 8]
        push eax
        call SavegameCopyBlock
        add esp, 0Ch
    savegame_copy_skip:
        mov DWORD PTR [edi], esi
        xor eax, eax
        pop edi
        pop esi
        pop ebp
        ret 14h
    }
}

namespace
{
    // SavegameFix: two anchors, eleven call retargets and one two-byte NOP, in
    // the original handler's write order. The call displacements are excluded
    // from the parity comparison; the NOP is not.
    void InstallSavegameFix()
    {
        if (!Loader::Enabled("SavegameFix"))
            return;

        auto pattern = find_pattern("75 ? 83 C1 01 83 C0 01 83 F9 04");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            injector::MakeCALL(match - 73, reinterpret_cast<void*>(&SavegameCloseBody), true);
            injector::MakeCALL(match - 143, reinterpret_cast<void*>(&SavegameCopyBody), true);
            injector::MakeCALL(match - 176, reinterpret_cast<void*>(&SavegameCopyBody), true);
            injector::MakeCALL(match - 280, reinterpret_cast<void*>(&SavegameMutexBody), true);
            injector::MakeCALL(match - 240, reinterpret_cast<void*>(&SavegameNullBody), true);
            injector::MakeNOP(match, 2, true);
        }

        pattern = find_pattern("8D 4C 24 18 51 6A 00 6A 0C 57");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            injector::MakeCALL(match - 86, reinterpret_cast<void*>(&SavegameMutexBody), true);
            injector::MakeCALL(match - 47, reinterpret_cast<void*>(&SavegameNullBody), true);
            injector::MakeCALL(match + 10, reinterpret_cast<void*>(&SavegameCopyBody), true);
            injector::MakeCALL(match + 90, reinterpret_cast<void*>(&SavegameCopyBody), true);
            injector::MakeCALL(match + 128, reinterpret_cast<void*>(&SavegameCloseBody), true);
        }
    }
}

// LocalSettingsPath, as the original handler sub_100513A0 applies it:
//
//   * two operand writes at the game's local-settings function (game 0x61E770):
//     the `call dword ptr [SHGetFolderPathA]` slot becomes this module's four-byte
//     call slot (original off_10158064, which pointed the game at its replacement
//     path helper sub_10051320), and the `push "Rockstar Games\GTA IV"` argument
//     becomes the "\GTA IV" suffix this module owns;
//   * a jump at the epilogue of the wide path initializer (game 0x856EE6) to the
//     detour body, and the same at the fallback epilogue (a site that does not
//     exist on 1.0.8.0);
//   * five captured game pointers the detour's step helper rewrites.
namespace
{
    // The size the original's strcpy_s/wcscpy_s copies pass, and the size of the
    // CWD buffer its replacement path helper formats into.
    constexpr std::size_t kLocalPathCapacity = 0x200u;
    constexpr std::size_t kLocalCwdCapacity = 260u;

    // The published state, in C linkage because the detour body's helper reads the
    // slots the way the original's bodies read its own .bss.
    char zpatch_local_ansi_cwd[kLocalCwdCapacity] = {};
    char zpatch_local_iv_suffix[8] = "\\GTA IV";
    uint32_t zpatch_local_saves_slot = 0;
    char* zpatch_local_path_08 = nullptr;          // szPathLocalAppdata
    wchar_t* zpatch_local_path_10 = nullptr;       // wszPathLocalAppdataW_EP
    const int* zpatch_local_episode_ptr = nullptr; // __dwCurrentEpisode
    char* zpatch_local_path_18 = nullptr;          // szPathLocalAppdata_EP
    wchar_t* zpatch_local_path_1c = nullptr;       // wszPathLocalAppdataW

    // A captured pattern operand is an address in the image's own coordinate
    // system. The dump places that image at a heap address, so the operand is
    // rebased through the image's preferred base; in the game the two are equal.
    template <typename T>
    T* GamePointer(uint32_t value)
    {
        const auto host = Game::Get().ImageBase();
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(host);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(host + dos->e_lfanew);
        return reinterpret_cast<T*>(host + (value - static_cast<uint32_t>(nt->OptionalHeader.ImageBase)));
    }

    // The path text the step helper stores: "\saves\<name>\".
    void BuildAnsiPath(char* out, const char* name)
    {
        std::size_t offset = 0;
        for (const char character : std::string_view("\\saves\\"))
            out[offset++] = character;
        for (const char* character = name; *character != '\0'; ++character)
            out[offset++] = *character;
        out[offset++] = '\\';
        out[offset] = '\0';
    }

    void BuildWidePath(wchar_t* out, const wchar_t* name)
    {
        std::size_t offset = 0;
        for (const wchar_t character : std::wstring_view(L"\\saves\\"))
            out[offset++] = character;
        for (const wchar_t* character = name; *character != L'\0'; ++character)
            out[offset++] = *character;
        out[offset++] = L'\\';
        out[offset] = L'\0';
    }

    // The original copies with strcpy_s/wcscpy_s at the same capacity; these write
    // the same bytes without the secure-copy path.
    void CopyText(char* destination, std::size_t capacity, const char* source)
    {
        std::size_t length = 0;
        while (length + 1 < capacity && source[length] != '\0')
        {
            destination[length] = source[length];
            ++length;
        }
        destination[length] = '\0';
    }

    void CopyWideText(wchar_t* destination, std::size_t capacity, const wchar_t* source)
    {
        std::size_t length = 0;
        while (length + 1 < capacity && source[length] != L'\0')
        {
            destination[length] = source[length];
            ++length;
        }
        destination[length] = L'\0';
    }
}

// The detour body's helper (original sub_10050D10): rewrite the four settings and
// save path buffers the initializer has just computed to the relative
// "\saves\<episode>\" form. The first ANSI destination is hardcoded "GTA IV"; the
// other three follow the episode counter. Called between pusha/popa, so it may
// clobber the general registers.
static void LocalSettingsStep()
{
    const int episode = zpatch_local_episode_ptr != nullptr ? *zpatch_local_episode_ptr : 0;
    const char* ansiName = "GTA IV";
    const wchar_t* wideName = L"GTA IV";
    if (episode == 1)
    {
        ansiName = "TLaD";
        wideName = L"TLaD";
    }
    else if (episode == 2)
    {
        ansiName = "TBoGT";
        wideName = L"TBoGT";
    }

    char ansiIv[32];
    char ansiEpisode[32];
    wchar_t wideEpisode[32];
    BuildAnsiPath(ansiIv, "GTA IV");
    BuildAnsiPath(ansiEpisode, ansiName);
    BuildWidePath(wideEpisode, wideName);

    if (zpatch_local_path_18 != nullptr)
        CopyText(zpatch_local_path_18, kLocalPathCapacity, ansiIv);
    if (zpatch_local_path_08 != nullptr)
        CopyText(zpatch_local_path_08, kLocalPathCapacity, ansiEpisode);
    if (zpatch_local_path_1c != nullptr)
        CopyWideText(zpatch_local_path_1c, kLocalPathCapacity, wideEpisode);
    if (zpatch_local_path_10 != nullptr)
        CopyWideText(zpatch_local_path_10, kLocalPathCapacity, wideEpisode);
}

// sub_100512F0, recovered VA 0x100512F0 (7 instructions): the wide path
// initializer's epilogue detour (game 0x856EE6). It runs the step helper between
// pusha/popa and replays the three pops and the return the five-byte jump
// swallowed. `pusha`/`popa` are emitted as bytes: the inline assembler renders
// both mnemonics with a 0x66 operand-size prefix, which the original does not
// have.
static __declspec(naked) void LocalSettingsDetourBody()
{
    __asm
    {
        __emit 0x60  // pusha
        call LocalSettingsStep
        __emit 0x61  // popa
        pop edi
        pop esi
        pop ebx
        ret
    }
}

// sub_10051300, recovered VA 0x10051300 (11 instructions): the fallback epilogue
// detour (a site that does not exist on 1.0.8.0). It runs the same helper, then
// re-issues the four-argument call the site held through ESI and replays the two
// pops and the return. Register ESI is the call target on entry, so the body
// saves it through pusha/popa and uses the live value.
static __declspec(naked) void LocalSettingsFallbackBody()
{
    __asm
    {
        __emit 0x60  // pusha
        call LocalSettingsStep
        __emit 0x61  // popa
        push 0
        push 0
        push 1Ch
        push 0
        call esi
        pop esi
        pop edi
        ret
    }
}

namespace
{
    // The replacement for the game's SHGetFolderPathA call at 0x61E770 (the
    // original's sub_10051320, __stdcall with five arguments): the fifth argument
    // is the destination, which receives "<cwd>\saves". The original builds the
    // text through the CRT and copies it with strcpy_s(dest, 0x104, ...); this
    // writes the same bytes with a bounded loop.
    int __stdcall LocalSettingsCopySaves(uint32_t, uint32_t, uint32_t, uint32_t, char* destination)
    {
        if (destination != nullptr)
        {
            std::size_t length = 0;
            while (length + 1 < kLocalCwdCapacity && zpatch_local_ansi_cwd[length] != '\0')
            {
                destination[length] = zpatch_local_ansi_cwd[length];
                ++length;
            }
            for (const char character : std::string_view("\\saves"))
            {
                if (length + 1 >= kLocalCwdCapacity)
                    break;
                destination[length++] = character;
            }
            destination[length] = '\0';
        }
        return 0;
    }

    // The original's static initializer captures the CWD and points its call slot
    // at the replacement helper; the handler only retargets the game's call to that
    // slot. Captured at load, before the game runs.
    void CaptureLocalSettings()
    {
        GetCurrentDirectoryA(static_cast<DWORD>(kLocalCwdCapacity), zpatch_local_ansi_cwd);
        zpatch_local_saves_slot =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LocalSettingsCopySaves));
    }

    void PublishLocalSettingsPath()
    {
        if (!Loader::Enabled("LocalSettingsPath"))
            return;

        CaptureLocalSettings();

        auto pattern = find_pattern("7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D6 5F 5E 5B C3");
        if (!pattern.empty())
        {
            // The wide path initializer's epilogue: each operand pushes one game
            // path buffer, or loads the episode counter.
            zpatch_local_path_1c = GamePointer<wchar_t>(*pattern.get_first<uint32_t>(-178));
            zpatch_local_path_10 = GamePointer<wchar_t>(*pattern.get_first<uint32_t>(-190));
            zpatch_local_episode_ptr = GamePointer<const int>(*pattern.get_first<uint32_t>(-151));
            zpatch_local_path_18 = GamePointer<char>(*pattern.get_first<uint32_t>(-444));
            zpatch_local_path_08 = GamePointer<char>(*pattern.get_first<uint32_t>(-432));
        }

        pattern = find_pattern("7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D7 5F C3");
        if (!pattern.empty())
        {
            // The alternate epilogue (absent on 1.0.8.0) stores the same two
            // LocalAppdata buffers the wide pattern captures.
            zpatch_local_path_18 = GamePointer<char>(*pattern.get_first<uint32_t>(-160));
            zpatch_local_path_1c = GamePointer<wchar_t>(*pattern.get_first<uint32_t>(-55));
        }
    }

    void InstallLocalSettingsPath()
    {
        if (!Loader::Enabled("LocalSettingsPath"))
            return;

        auto pattern = find_pattern("53 8B 5C 24 08 56 6A 00 6A 00");
        if (!pattern.empty())
        {
            // +16 is the call's memory operand, +32 the pushed path-name operand.
            // Both addresses point into this module; the write-set dump expresses
            // them in the baseline's coordinates.
            auto* match = pattern.get_first<std::byte>(0);
            injector::WriteMemory<uint32_t>(match + 16,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_local_saves_slot)), true);
            injector::WriteMemory<uint32_t>(match + 32,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(zpatch_local_iv_suffix)), true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(match + 16));
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(match + 32));
        }

        pattern = find_pattern("7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D6 5F 5E 5B C3");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(14),
                reinterpret_cast<void*>(&LocalSettingsDetourBody), true);

        pattern = find_pattern("7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D7 5F C3");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(-13),
                reinterpret_cast<void*>(&LocalSettingsFallbackBody), true);

        // The detour body's inline `call` is invisible to C++; naming the helper
        // here keeps its out-of-line definition in the image.
        const auto step = reinterpret_cast<uintptr_t>(&LocalSettingsStep);
        (void)step;
    }
}

// Registered like every other arm: the install tier owns the gate.
export void RegisterLoading()
{
    Loader::PublishEvent() += []
    {
        PublishLocalSettingsPath();
        CopyOverrideDatPath();
    };
    Loader::InstallEvent() += []
    {
        InstallFastLoading();
        InstallNoLoadingSleep();
        InstallLocalSettingsPath();
        InstallRemoveGFWLUpdatePopup();
        InstallRemoveRGSCLoginRequirement();
        InstallStartupTimeFix();
        InstallVSyncFix();
        InstallBenchmarkFix();
        InstallOverrideGTADatPath();
        InstallSkipIntro();
        InstallSkipMenu();
        InstallSavegameFix();
    };

    // The hook bodies carry their recovered VAs so the dump can express every
    // branch in the original's coordinates.
    Bodies::Register(reinterpret_cast<uintptr_t>(&LocalSettingsDetourBody), 0x100512F0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&LocalSettingsFallbackBody), 0x10051300u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&GfwlStubBody), 0x100054E0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&RgscLoginRemovedBody), 0x10038550u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&BenchmarkBodyA), 0x10005650u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&BenchmarkBodyB), 0x10005670u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SkipIntroBody), 0x10045C30u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SkipMenuBody), 0x10046610u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SavegameNullBody), 0x10038570u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SavegameCloseBody), 0x10045580u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SavegameCopyBody), 0x100455A0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SavegameMutexBody), 0x100455E0u);

    // The two imports the save bodies call: the original calls them through its
    // own IAT slots, so the bodies call through these published addresses.
    zpatch_savegame_close_handle = reinterpret_cast<uintptr_t>(&CloseHandle);
    zpatch_savegame_create_mutex = reinterpret_cast<uintptr_t>(&CreateMutexA);

    // The copy body's inline `call` is invisible to C++; naming the wrapper here
    // keeps its out-of-line definition in the image.
    const auto copyBlock = reinterpret_cast<uintptr_t>(&SavegameCopyBlock);
    (void)copyBlock;
}

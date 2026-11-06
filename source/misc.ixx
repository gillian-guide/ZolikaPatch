module;

#include <common.hxx>

export module misc;

import bodies;
import bytes;
import common;
import game;
import loader;
import native;
import patch;

// The remaining fixes: the bundled MiscFixes table, the mod-check stubs and the
// drive-by FOV. One arm per key, each anchored to its own recovered signature (or,
// for the stubs, to the recovered per-build site table).

// The DrivebyFOVFix published state, in C linkage because the transcribed bodies
// name the slots directly (the original keeps them in its own .bss beside its
// bodies). The values are the original's own: dword_101580D4 is the native hash the
// helper resolves (the loader preamble writes 0x09399528 at 0x10052E6E),
// dword_101580C4 is the FOV it caches (45.0f, the .data value and the helper's own
// first instruction), dword_1015BDF8 the cached entry pointer, byte_1015AD4A the
// one-shot flag, and dword_1015BDF4 the resume the arm publishes.
extern "C"
{
    uint32_t zpatch_driveby_salt = 0x09399528u;
    float zpatch_driveby_fov = 45.0f;
    uint32_t zpatch_driveby_cached = 0;
    uint8_t zpatch_driveby_done = 0;
    uint32_t zpatch_driveby_resume = 0;

    // The native layer's resolve entry (source/native.ixx), for the helper's call.
    extern uintptr_t zpatch_native_resolve_entry;
}

// The helper's call target. The original's helper does `call sub_10003980` (E8), so
// ours must be a direct call; a module interface cannot name an imported function
// from inline asm, so this six-byte forwarder jumps through the entry the native
// layer publishes. ECX (the hash) and the stack reach the layer untouched, and the
// layer's __try/__except frame is the one the original's SEH entry installs.
static __declspec(naked) void DrivebyResolveEntry()
{
    __asm
    {
        jmp DWORD PTR [zpatch_native_resolve_entry]
    }
}

// sub_10006450, recovered VA 0x10006450 (20 instructions): computes the drive-by
// FOV once and caches the entry it reads it from. It resolves the native hash
// dword_101580D4 through the call layer, walks the handler's two structures at
// +0Eh/+0Fh/+14h, stores the entry in dword_1015BDF8 and reads the float at +60h
// into dword_101580C4. The one-shot flag makes every later call only refresh the
// default. The resolver result and the cached entry are dereferenced unguarded --
// the same faults the original has (0x1000646E and 0x10006490) -- and 45.0f is
// stored on every call, its own first instruction.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void DrivebyFovHelperBody()
{
    __asm
    {
        cmp BYTE PTR [zpatch_driveby_done], 0
        mov DWORD PTR [zpatch_driveby_fov], 42340000h  // 45.0f
        jne driveby_have
        mov ecx, DWORD PTR [zpatch_driveby_salt]
        call DrivebyResolveEntry
        mov ecx, DWORD PTR [eax + 0Eh]
        add eax, 0Eh
        add ecx, eax
        mov BYTE PTR [zpatch_driveby_done], 1
        mov eax, DWORD PTR [ecx + 0Fh]
        mov eax, DWORD PTR [eax + ecx + 14h]
        mov DWORD PTR [zpatch_driveby_cached], eax
        jmp driveby_read
    driveby_have:
        mov eax, DWORD PTR [zpatch_driveby_cached]
    driveby_read:
        mov eax, DWORD PTR [eax]
        test eax, eax
        je driveby_done
        movss xmm0, DWORD PTR [eax + 60h]
        movss DWORD PTR [zpatch_driveby_fov], xmm0
    driveby_done:
        ret
    }
}

// sub_100064B0, recovered VA 0x100064B0 (5 instructions): the site's shim. It saves
// every general register (the site resumes with its own GPRs and flags intact),
// computes the FOV, restores them, loads the value into xmm0 and tail-jumps to the
// site's own remaining `subss xmm1, xmm2` at the published resume.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void DrivebyFovShimBody()
{
    __asm
    {
        __emit 0x60  // pushad (pushal)
        call DrivebyFovHelperBody
        __emit 0x61  // popad (popal)
        movss xmm0, DWORD PTR [zpatch_driveby_fov]
        jmp DWORD PTR [zpatch_driveby_resume]
    }
}

// `mov eax, imm32`: the MiscFixes token-activation row writes it. bytes.ixx does
// not carry it, and that module is outside this task's edit scope, so the opcode
// is named here.
// The opcode comes from bytes.ixx; this table only records which row writes it.

namespace
{
    // One site of MiscFixes: the recovered signature, the write shape, the offset
    // from the anchor, and the byte the shape writes (the jump displacement for a
    // short jump, the immediate for the `mov eax` row).
    enum class MiscFixWrite : uint8_t
    {
        byte,
        shortJump,
        nop2,
        movEax,
    };

    struct MiscFixSite final
    {
        std::string_view signature;
        MiscFixWrite kind;
        ptrdiff_t offset;
        std::byte opcode{};  // the byte for `byte`, the jmp opcode for `shortJump`
        uint32_t value{};    // the jump displacement, or the `mov eax` immediate
    };

    // The original handler's eleven literals in its write order (sub_10023DD0). The
    // DFA stage it calls between them is a loader-level arm there, not part of this
    // key's write set. Five rows are other builds' sites and match nothing on
    // 1.0.8.0, which is why the key writes eight places on that image.
    constexpr MiscFixSite kMiscFixSites[] = {
        // data integrity checks VDS102 [v1002 - v1004]
        { "C7 06 ? ? ? ? 74 19", MiscFixWrite::byte, 6, Bytes::ShortJmp },
        // data integrity checks VDS102 [v1005 - v1008]
        { "83 C4 ? 3B 46 20", MiscFixWrite::byte, 12, Bytes::ShortJmp },
        // data integrity checks VDS100 [v1006 - v1008]
        { "75 28 6A 00 6A 00 68 ? ? ? ? E8 ? ? ? ? 83 C4 0C", MiscFixWrite::byte, 0,
          Bytes::ShortJmp },
        // token activation [v1006 - v1007]: `jmp +0xC9`
        { "8B 54 24 04 52 A3", MiscFixWrite::shortJump, 0, Bytes::ShortJmp, 0xC9 },
        // token activation [v1006 - v1007]: mov eax,1
        { "83 F8 01 75 30 A3", MiscFixWrite::movEax, 0, Bytes::MovEax, 1 },
        // skip first-time play ask [v1006 - v1008]: `jmp +0x0D`
        { "C7 05 ? ? ? ? 03 00 00 00 33 C0 5E 59 C3 C7", MiscFixWrite::shortJump, 0,
          Bytes::ShortJmp, 0x0D },
        // InternetGetConnectedState [v1000 - v1001]
        { "FF 15 ? ? ? ? 85 C0 75 18", MiscFixWrite::nop2, 8 },
        // InternetGetConnectedState [v1002 - v1005]
        { "FF 15 ? ? ? ? 85 C0 75 04 32", MiscFixWrite::nop2, 8 },
        // InternetGetConnectedState [v1006 - v1008]
        { "FF 15 ? ? ? ? 85 C0 75 07 A0", MiscFixWrite::nop2, 8 },
        // health check [v1003 - v1008]
        { "81 EC ? ? ? ? A1 ? ? ? ? 33 C4 89 84 24 ? ? ? ? 53 ? 6A 3C 33 DB 8D 44 24 ? 53 50 E8",
          MiscFixWrite::byte, 0, Bytes::Ret },
        // the handler's last write, after its DFA call: force a short jump
        { "74 05 E8 ? ? ? ? 6A 05 E8 ? ? ? ? 83 C4 04 B9", MiscFixWrite::byte, 0,
          Bytes::ShortJmp },
    };

    // MiscFixes: the eleven literals, in the handler's order. The original skips
    // the whole key when the XLivelessAddon companion is loaded (it sets its
    // byte_10159B84 from GetModuleHandleA("XLivelessAddon.asi")), so the arm asks
    // the same question first.
    void InstallMiscFixes()
    {
        if (GetModuleHandleA("XLivelessAddon.asi") != nullptr)
            return;
        if (!Loader::Enabled("MiscFixes"))
            return;

        for (const auto& site : kMiscFixSites)
        {
            auto pattern = find_pattern(site.signature);
            if (pattern.empty())
                continue;

            auto* match = pattern.get_first<std::byte>(0);
            switch (site.kind)
            {
            case MiscFixWrite::byte:
                injector::WriteMemory<std::byte>(match + site.offset, site.opcode, true);
                break;
            case MiscFixWrite::shortJump:
                // A `jmp rel8` written as its two bytes: the injector has no
                // short-jump primitive.
                injector::WriteMemory<std::byte>(match + site.offset, site.opcode, true);
                injector::WriteMemory<std::byte>(match + site.offset + 1,
                    static_cast<std::byte>(site.value), true);
                break;
            case MiscFixWrite::nop2:
                injector::MakeNOP(match + site.offset, 2, true);
                break;
            case MiscFixWrite::movEax:
                injector::WriteMemory<std::byte>(match + site.offset, site.opcode, true);
                injector::WriteMemory<uint32_t>(match + site.offset + 1, site.value, true);
                break;
            }
        }
    }

    // Patch6and7RemoveModChecks (original handler sub_10044710): the packed
    // entry-stub jump targets, per gated build. The handler writes
    // `lea edi,[eax+<site>]; lea ebx,[eax+<target>]; mov byte [edi],0E9h`; the
    // entry target (0x3A09) is a 1.0.6.0 row, the mid and case targets are
    // 1.0.7.0 rows.
    constexpr uint32_t kModChecksEntryJmp1060 = 0x003A09u;
    constexpr uint32_t kModChecksMidJmp1070 = 0x0038AEu;
    constexpr uint32_t kModChecksCaseJmp1070 = 0x0039E9u;

    // One site of the two arms: the byte the handler leaves at the match, or the
    // target it writes a five-byte jump to. Every candidate has exactly one hit at
    // its site on its build.
    enum class ModCheckWrite : uint8_t
    {
        zero,       // clear the stub byte
        jmpOpcode,  // `jmp rel8` (the branch operand already there stays)
        jump,       // `jmp rel32` to `target`
        nopSeven,   // wipe the mod enabler native's seven bytes
    };

    struct ModCheckSite final
    {
        std::string_view signature;
        ModCheckWrite kind;
        uint32_t target{};  // jump only
    };

    // sub_10044710's write order, per build (1.0.6.0 and 1.0.7.0 are the only
    // gated builds; the handler's own `lea edi,[eax+<site>]` rows agree).
    constexpr ModCheckSite kModChecks1060[] = {
        { "01 00 00 00 51 89 5C 24 ? E8 ? ? ? ? 85 C0 75 ? 39 44 24 ? 74",
          ModCheckWrite::zero },
        { "74 ? 8B 45 ? 53 6A ? 50 E8 ? ? ? ? 57 E8 ? ? ? ? 83 C4 ? 68",
          ModCheckWrite::jmpOpcode },
        { "01 00 00 00 75 ? 8B 4E ? 8D 44 24 ? 50 6A ? 51 89 7C ? ? E8",
          ModCheckWrite::zero },
        { "75 ? 8B 4E ? 8D 44 24 ? 50 6A ? 51 89 7C 24 ? E8", ModCheckWrite::jump,
          kModChecksEntryJmp1060 },
        { "74 ? 8B 4D ? 57 6A ? 51 E8 ? ? ? ? 80 3D ? ? ? ? ? 0F",
          ModCheckWrite::jmpOpcode },
        { "C6 05 ? ? ? ? ? 74 ? 8B 40 ? 56 6A ? 6A ? 50 83 CE ? E8",
          ModCheckWrite::nopSeven },
    };

    constexpr ModCheckSite kModChecks1070[] = {
        { "01 00 00 00 51 89 5C 24 ? E8 ? ? ? ? 85 C0 75 ? 39 44 24 ? 74",
          ModCheckWrite::zero },
        { "75 ? 39 44 24 ? 74 ? 8A C3 5F 5E 5D 5B 83 C4 ? C3 8D 54 24 ? 52 8D",
          ModCheckWrite::jump, kModChecksMidJmp1070 },
        { "74 ? 8B 45 ? 53 6A ? 50 E8 ? ? ? ? 57 E8 ? ? ? ? ? ? ? 68",
          ModCheckWrite::jmpOpcode },
        { "01 00 00 00 75 ? 8B 4E ? 8D 44 24 ? 50 6A ? 51 89 7C ? ? E8",
          ModCheckWrite::zero },
        { "75 ? 8B 4E ? 8D 44 24 ? 50 6A ? 51 89 7C 24 ? E8", ModCheckWrite::jump,
          kModChecksCaseJmp1070 },
        { "74 ? 8B 4D ? 57 6A ? 51 E8 ? ? ? ? 80 3D ? ? ? ? ? 0F",
          ModCheckWrite::jmpOpcode },
        { "C6 05 ? ? ? ? ? 74 ? 8B 40 ? 56 6A ? 6A ? 50 83 CE ? E8",
          ModCheckWrite::nopSeven },
    };

    struct ModCheckBuildSites final
    {
        Build build;
        std::span<const ModCheckSite> sites;
    };

    // The build gate: a build without a row writes nothing.
    constexpr ModCheckBuildSites kModChecksSites[] = {
        { Build::v1060, kModChecks1060 },
        { Build::v1070, kModChecks1070 },
    };

    void InstallPatch6and7RemoveModChecks()
    {
        if (!Loader::Enabled("Patch6and7RemoveModChecks"))
            return;

        for (const auto& row : kModChecksSites)
        {
            if (row.build != Loader::Current())
                continue;

            for (const auto& site : row.sites)
            {
                auto pattern = find_pattern(site.signature);
                if (pattern.empty())
                    continue;

                auto* match = pattern.get_first<std::byte>(0);
                switch (site.kind)
                {
                case ModCheckWrite::zero:
                    injector::WriteMemory<std::byte>(match, Bytes::Zero, true);
                    break;
                case ModCheckWrite::jmpOpcode:
                    injector::WriteMemory<std::byte>(match, Bytes::ShortJmp, true);
                    break;
                case ModCheckWrite::jump:
                    injector::MakeJMP(match,
                        reinterpret_cast<void*>(Game::Get().ImageBase() + site.target), true);
                    break;
                case ModCheckWrite::nopSeven:
                    injector::MakeNOP(match, 7, true);
                    break;
                }
            }
        }
    }

    // DrivebyFOVFix: the site is the `movss xmm0,[esi+60h]` in front of the
    // per-frame drive-by FOV subtraction. The publish reads the site's own next
    // instruction (the `subss xmm1, xmm2` at the anchor's +5) for the resume before
    // the install overwrites the five bytes the jump lands on.
    void PublishDrivebyFOVFix()
    {
        if (!Loader::Enabled("DrivebyFOVFix"))
            return;

        auto pattern = find_pattern("F3 0F 10 46 60 F3 0F 5C CA");
        if (!pattern.empty())
            zpatch_driveby_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(5)));
    }

    void InstallDrivebyFOVFix()
    {
        if (!Loader::Enabled("DrivebyFOVFix"))
            return;

        auto pattern = find_pattern("F3 0F 10 46 60 F3 0F 5C CA");
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&DrivebyFovShimBody), 0x100064B0u);
            Bodies::Register(reinterpret_cast<uintptr_t>(&DrivebyFovHelperBody), 0x10006450u);
            // The helper's call names the original's guarded resolve entry; recording
            // that VA keeps every registered branch in the original's coordinates.
            Bodies::Register(reinterpret_cast<uintptr_t>(&DrivebyResolveEntry), 0x10003980u);
            injector::MakeJMP(pattern.get_first(0),
                reinterpret_cast<void*>(&DrivebyFovShimBody), true);
        }
    }
}

// Four of this module's keys are deliberately not installed:
//
//   * SpeedupBugFix: the body is sub_10050540 (109 instructions) -- compiled code
//     that fills the game's eight timing floats from QueryPerformanceFrequency and
//     stores a 64-bit timestamp -- and the arm has to publish eight absolute float
//     pointers (the anchor's +26 .. +276 operands) for it.
//   * Patch4RemoveModChecks: the 8-instruction stub sub_100054E0 calls
//     sub_10005340 (145 instructions), the flag-gated helper that does the scans
//     and writes.
//   * PopupHeadlightsFlag: the two 43/44-byte shims call sub_10044310 (125
//     instructions), the popuplights checker.
//   * CopSirensFlag: its three shims call sub_10005F70 (191 instructions),
//     sub_10006260 (53) and the IDE parser sub_10003FA0 (92).
//
// Each stays open rather than being stubbed. DrivebyFOVFix used to be here too:
// it needed the ASI's native-call layer, which source/native.ixx now provides.

// Registered like every other arm: the publish tier reads the site's original
// bytes, the install tier owns the gate.
export void RegisterMisc()
{
    Loader::PublishEvent() += []
    {
        PublishDrivebyFOVFix();
    };

    Loader::InstallEvent() += []
    {
        InstallMiscFixes();
        InstallPatch6and7RemoveModChecks();
        InstallDrivebyFOVFix();
    };
}

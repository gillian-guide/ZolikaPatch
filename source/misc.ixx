module;

#include <common.hxx>
#include <cstdlib>
#include <cstring>

export module misc;

import bodies;
import bytes;
import common;
import config;
import game;
import loader;
import native;
import patch;
import shared;

// The remaining fixes: the bundled MiscFixes table, the mod-check stubs and the
// drive-by FOV. One arm per key, each anchored to its own recovered signature (or,
// for the stubs, to the recovered per-build site table).

// The DrivebyFOVFix published state, in C linkage because the transcribed bodies
// name the slots directly (the original keeps them in its own .bss beside its
// bodies). dword_101580D4 is the native hash the helper resolves; it is not seeded
// here, because the original's loader preamble resolves it with its ten-word salt
// rule (source/native.ixx): on 1.0.8.0 the probe hits and the slot keeps the
// shipped `.data` value 0x75E005F1, while a probe miss substitutes 0x09399528.
// dword_101580C4 is the FOV it caches (45.0f, the .data value and the helper's own
// first instruction), dword_1015BDF8 the cached entry pointer, byte_1015AD4A the
// one-shot flag, and dword_1015BDF4 the resume the arm publishes.
extern "C"
{
    // The layer's resolved salt (source/native.ixx), dword_101580D4.
    extern uint32_t zpatch_native_salt_101580D4;

    float zpatch_driveby_fov = 45.0f;
    uint32_t zpatch_driveby_cached = 0;
    uint8_t zpatch_driveby_done = 0;
    uint32_t zpatch_driveby_resume = 0;

    // The native layer's resolve entry (source/native.ixx), for the helper's call.
    extern uintptr_t zpatch_native_resolve_entry;

    // SpeedupBugFix: the eight game timing floats the handler publishes, in its
    // own read order (the last four repeat the first four for the episode path).
    // The pointers are game addresses read from the site's operands; the body
    // writes through them.
    float* zpatch_speedup_t4c = nullptr;  // dword_1015934C: copy of dword_18CAEB0
    float* zpatch_speedup_t50 = nullptr;  // dword_10159350: dword_18CAE9C, seconds per tick
    float* zpatch_speedup_t54 = nullptr;  // dword_10159354: copy of dword_18CAE9C
    float* zpatch_speedup_t58 = nullptr;  // dword_10159358: copy of dword_1752074
    float* zpatch_speedup_t5c = nullptr;  // dword_1015935C: dword_1752078, 1 / milliseconds
    float* zpatch_speedup_t60 = nullptr;  // dword_10159360: dword_18CAEA4, milliseconds * dword_FF2014
    float* zpatch_speedup_t64 = nullptr;  // dword_10159364: flt_18CAEA0, milliseconds per tick
    float* zpatch_speedup_t68 = nullptr;  // dword_10159368: copy of flt_18CAEA0

    // Patch4RemoveModChecks: dword_10159C18, the continuation (match + 12) of the
    // game's date arithmetic. loading.ixx owns the same original slot for its
    // RemoveGFWLUpdatePopup arm; each arm publishes its own copy because the two
    // arms live in different modules.
    uint32_t zpatch_patch4_resume = 0;
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
        mov ecx, DWORD PTR [zpatch_native_salt_101580D4]
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

// SpeedupBugFix: the game's timer-init function (1.0.8.0 game 0x4571B0, the
// `50 64 89 25 00 00 00 00 83 EC 34` prologue's match - 21) is replaced by the
// original's sub_10050540. That body is compiled C -- an SEH frame, a /GS
// cookie, the MSVC thread-safe static guard for its one-time signature scan and
// SSE double arithmetic -- so this is a C++ reimplementation of the same
// behaviour, not a transcription (docs/parity.md records the split).
//
// The original's handler has no build gate and reads its eight float pointers
// from fixed site-relative operands, so the row is the same on every build that
// carries the signature (0x4544B0 on 1.0.4.0 through 0x5AD550 on 1.1.3.0) and
// 1.0.0.0, which has no hit, is a clean skip. tests/signatures.tsv records the
// per-build sites and the eight operand encodings.
//
// It fills the game's eight timing floats from QueryPerformanceFrequency, then
// stores the game clock's 64-bit stamp in *self and answers self. The clock
// function is found by the original's own one-time scan ("1B D9 89 5C 24 24 0F
// 88", 1.0.8.0 site 0x456F93) and is called at match - 51 = 0x456F60.
static std::atomic<uint32_t> zpatch_speedup_scan_done{ 0 };
static uint32_t zpatch_speedup_clock = 0;

static void* __fastcall SpeedupBugFixBody(void* self, int)
{
    // sub_10050587's MSVC static-init guard: the scan runs once per process. A
    // miss leaves the clock at zero, and the faithful `0 - 51` call below faults
    // on 0xFFFFFFCD exactly as the original's does.
    if (zpatch_speedup_scan_done.exchange(1u, std::memory_order_acq_rel) == 0)
    {
        auto pattern = find_pattern("1B D9 89 5C 24 24 0F 88");
        if (!pattern.empty())
            zpatch_speedup_clock = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)));
    }

    // The original tests the float, not the pointer, so the dereference stays
    // unguarded; the arm installs this body only after all eight pointers were
    // published.
    if (*zpatch_speedup_t50 == 0.0f)
    {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);

        // 0x10152380 is 1000.0 and 0x10152300 is 0.0010000000474974513; the
        // original divides, multiplies and truncates to float in this order.
        const double scale = 1000.0 / static_cast<double>(frequency.QuadPart);
        const float perTickMilliseconds = static_cast<float>(scale);
        const float millisecondsProduct = static_cast<float>(scale * 1000.0);
        const float perTickSeconds = static_cast<float>(scale * 0.0010000000474974513);
        const float reciprocal = static_cast<float>(0.0010000000474974513 / scale);

        *zpatch_speedup_t64 = perTickMilliseconds;
        *zpatch_speedup_t60 = millisecondsProduct;
        *zpatch_speedup_t50 = perTickSeconds;
        *zpatch_speedup_t5c = reciprocal;
        *zpatch_speedup_t68 = perTickMilliseconds;
        *zpatch_speedup_t4c = millisecondsProduct;
        *zpatch_speedup_t54 = perTickSeconds;
        *zpatch_speedup_t58 = reciprocal;
    }

    using ClockFunction = __int64(__cdecl*)(void);
    const __int64 stamp = reinterpret_cast<ClockFunction>(
        static_cast<uintptr_t>(zpatch_speedup_clock) - 51u)();
    *reinterpret_cast<__int64*>(self) = stamp;
    return self;
}

namespace
{
    // A captured pattern operand is an address in the image's own coordinate
    // system. The dump places that image at a heap address, so the operand is
    // rebased through the image's preferred base; in the game the two are equal.
    template <typename T>
    T* GameImagePointer(uint32_t value)
    {
        const auto host = Game::Get().ImageBase();
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(host);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(host + dos->e_lfanew);
        return reinterpret_cast<T*>(host + (value - static_cast<uint32_t>(nt->OptionalHeader.ImageBase)));
    }

    // SpeedupBugFix: the handler's eight operands, in its read order
    // (`mov eax,[ebx+2Fh]` .. `mov eax,[ebx+129h]`, ebx = match - 21), are the
    // absolute addresses of the floats the replacement body computes. Each one is
    // the disp32 field of a `movss` in the timer-init function, so the four bytes
    // before the field are that instruction's opcode and ModRM. Those bytes are the
    // per-build evidence that the dword really is one of the timing floats and not
    // something that happens to sit at the same distance from a coincidental
    // signature match: they are identical on every image that carries the
    // signature (tests/signatures.tsv records the recovered values).
    struct TimingOperand final
    {
        ptrdiff_t offset;               // the handler's [ebx+disp], from the site
        std::array<uint8_t, 4> opcode;  // the instruction bytes that precede it
        float** slot;
    };

    constexpr TimingOperand kTimingOperands[] = {
        { 0x02F, { 0xF3, 0x0F, 0x10, 0x05 }, &zpatch_speedup_t50 },  // movss xmm0,[dword_18CAE9C]
        { 0x0BA, { 0xF3, 0x0F, 0x11, 0x0D }, &zpatch_speedup_t64 },  // movss [flt_18CAEA0],xmm1
        { 0x0D1, { 0xF3, 0x0F, 0x11, 0x05 }, &zpatch_speedup_t60 },  // movss [dword_18CAEA4],xmm0
        { 0x0F7, { 0xF3, 0x0F, 0x11, 0x1D }, &zpatch_speedup_t5c },  // movss [dword_1752078],xmm3
        { 0x0FF, { 0xF3, 0x0F, 0x11, 0x0D }, &zpatch_speedup_t68 },  // movss [dword_18CAEAC],xmm1
        { 0x10E, { 0xF3, 0x0F, 0x11, 0x1D }, &zpatch_speedup_t4c },  // movss [dword_18CAEB0],xmm3
        { 0x11D, { 0xF3, 0x0F, 0x11, 0x15 }, &zpatch_speedup_t54 },  // movss [dword_18CAEA8],xmm2
        { 0x129, { 0xF3, 0x0F, 0x11, 0x05 }, &zpatch_speedup_t58 },  // movss [dword_1752074],xmm0
    };

    constexpr size_t kTimingOperandCount = sizeof(kTimingOperands) / sizeof(kTimingOperands[0]);

    // The body writes through all eight pointers, so every one of them has to be a
    // writable word of the image in use before any of them is published: a
    // signature that matched a different function would otherwise hand the body a
    // code address or a constant to store floats into.
    bool WritableImageWord(uint32_t value)
    {
        const auto host = Game::Get().ImageBase();
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(host);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(host + dos->e_lfanew);
        const auto preferred = static_cast<uint32_t>(nt->OptionalHeader.ImageBase);
        if (value < preferred ||
            value - preferred > nt->OptionalHeader.SizeOfImage - sizeof(uint32_t))
            return false;

        const auto rva = value - preferred;
        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section)
        {
            if (rva < section->VirtualAddress || rva - section->VirtualAddress >= section->Misc.VirtualSize)
                continue;
            return (section->Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
        }
        return false;
    }

    // SpeedupBugFix's site, validated. The original's handler has no build gate --
    // the loader calls it whenever the key is on (`cmp esi,1; jnz; call
    // sub_100506B0`) and reads its eight operands at fixed site-relative offsets,
    // so its row is the same on every build: a hit installs `E9` at match-21 and a
    // miss is the handler's own `test eax,eax; jz` return. Reproducing that means
    // reproducing it only where the site is really the timer-init function; the
    // operand encodings and their writable targets are what say so, and a site that
    // fails either test is not this build's layout, so nothing is published and
    // nothing is installed.
    std::byte* ResolveSpeedupBugFix()
    {
        auto pattern = find_pattern("50 64 89 25 00 00 00 00 83 EC 34");
        if (pattern.empty())
            return nullptr;

        auto* site = pattern.get_first<std::byte>(-21);
        uint32_t targets[kTimingOperandCount];
        for (size_t index = 0; index < kTimingOperandCount; ++index)
        {
            const TimingOperand& operand = kTimingOperands[index];
            const auto* field = site + operand.offset;
            if (std::memcmp(field - operand.opcode.size(), operand.opcode.data(),
                    operand.opcode.size()) != 0)
                return nullptr;

            targets[index] = *reinterpret_cast<const uint32_t*>(field);
            if (!WritableImageWord(targets[index]))
                return nullptr;
        }

        for (size_t index = 0; index < kTimingOperandCount; ++index)
            *kTimingOperands[index].slot = GameImagePointer<float>(targets[index]);
        return site;
    }

    // The publish tier runs before the install tier, so the site resolved there is
    // the one the install patches; a null one keeps the install off.
    std::byte* zpatch_speedup_site = nullptr;

    // Publishing the eight floats before the install is what keeps the body's
    // writes on the game's own words; the install only replaces the function's
    // entry.
    void PublishSpeedupBugFix()
    {
        if (!Loader::Enabled("SpeedupBugFix"))
            return;

        zpatch_speedup_site = ResolveSpeedupBugFix();
    }

    void InstallSpeedupBugFix()
    {
        if (!Loader::Enabled("SpeedupBugFix"))
            return;

        if (zpatch_speedup_site != nullptr)
            injector::MakeJMP(zpatch_speedup_site, reinterpret_cast<void*>(&SpeedupBugFixBody),
                true);
    }
}

// Patch4RemoveModChecks: the game's date arithmetic at 1.0.8.0 game 0x7CBF70
// (the `2B C1 1B D6 ...` anchor) is replaced by the original's sub_100054E0, an
// 8-instruction stub that runs the shared flag-gated helper between pusha/popa,
// replays the 64-bit subtraction and its two stack stores, and resumes at
// match + 12.
//
// The helper (sub_10005340, 145 instructions) has two branches. The GFWL one is
// RemoveGFWLUpdatePopup's and already lives in loading.ixx; this module owns the
// Patch4 one: a one-time scan for "33 C4 89 84 24 A8 01 00 00 53 8B 5D 08 56 57"
// (the original's own scanner call), then a zero store to the dword the matched
// code points at (site + 30). That signature has no hit on 1.0.8.0, so on the
// user's build the branch is a scan and nothing else -- the key's visible effect
// there is the stub install itself, exactly as in the original. The scan is kept
// so builds where the target exists behave the same.
//
// The helper is compiler output for a C function (a thread-safe static guard and
// VirtualProtect calls), so it is rebuilt, not transcribed; the stub is
// transcribed and proven whole.
static std::atomic<uint32_t> zpatch_patch4_scan_done{ 0 };
static uint32_t zpatch_patch4_site = 0;

static void Patch4Step()
{
    if (zpatch_patch4_scan_done.exchange(1u, std::memory_order_acq_rel) == 0)
    {
        auto pattern = find_pattern("33 C4 89 84 24 A8 01 00 00 53 8B 5D 08 56 57");
        if (!pattern.empty())
            zpatch_patch4_site = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)));
    }

    if (zpatch_patch4_site != 0)
    {
        // 0x10005427 reads *[site + 30] without a null check; the zero store goes
        // through the injector, which is the original's VirtualProtect/write/restore.
        uint32_t target = 0;
        std::memcpy(&target,
            reinterpret_cast<const void*>(static_cast<uintptr_t>(zpatch_patch4_site) + 30u),
            sizeof(target));
        injector::WriteMemory<uint32_t>(target, 0u, true);
    }
}

// sub_100054E0, recovered VA 0x100054E0 (8 instructions). `pusha`/`popa` are
// emitted as bytes: the inline assembler renders both with a 0x66 operand-size
// prefix, which the original does not have.
static __declspec(naked) void Patch4StubBody()
{
    __asm
    {
        __emit 0x60  // pusha
        call Patch4Step
        __emit 0x61  // popa
        sub eax, ecx
        sbb edx, esi
        mov DWORD PTR [esp + 0Ch], eax
        mov DWORD PTR [esp + 10h], edx
        jmp DWORD PTR [zpatch_patch4_resume]
    }
}

namespace
{
    // Patch4RemoveModChecks: the anchor's first twelve bytes are the subtraction
    // and the two stores; the stub replays them and resumes at match + 12.
    void PublishPatch4RemoveModChecks()
    {
        if (!Loader::Enabled("Patch4RemoveModChecks"))
            return;

        auto pattern = find_pattern(
            "2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08");
        if (!pattern.empty())
            zpatch_patch4_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(12)));
    }

    void InstallPatch4RemoveModChecks()
    {
        if (!Loader::Enabled("Patch4RemoveModChecks"))
            return;

        auto pattern = find_pattern(
            "2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&Patch4StubBody), true);
    }
}

// CopSirensFlag: the two model checks (sub_10005F70 and sub_10006260) and the
// shared IDE hook (sub_100040E0), which fills the 512-slot model-hash tables the
// checks walk from the game's IDE parser entries.
extern "C"
{
    // dword_10159C0C, defined in source/shared.ixx: the game's model-index lookup,
    // `int lookup(uint32_t nameHash, int32_t *index)`.
    extern uintptr_t zpatch_model_lookup;

    // dword_1015937C / dword_10159374 (the resumes), dword_10159370 and
    // dword_1015936C (the two model indices the sites computed), dword_10159B80 /
    // dword_10159378 (the two flags), and the IDE parser's arguments and resume
    // (dword_1015BE08 the string, dword_1015BE0C the model object, dword_1015BE10
    // the continuation).
    uint32_t zpatch_copsirens_resume = 0;
    uint32_t zpatch_copsirens_resume_b = 0;
    uint32_t zpatch_copsirens_index = 0;
    uint32_t zpatch_copsirens_index_b = 0;
    uint32_t zpatch_copsirens_flag = 0;
    uint32_t zpatch_copsirens_flag_b = 0;
    uint32_t zpatch_ide_string = 0;
    uint32_t zpatch_ide_model = 0;
    uint32_t zpatch_ide_hook_slot = 0;

    // The 512-slot tables sub_10003FA0 fills: dword_10159380 (sirens),
    // dword_1015CE28 (popuplights), and dword_1015A538 / dword_10159D38
    // (wheelangle models and the float bits of their angles).
    uint32_t zpatch_copsirens_models[512] = {};
    uint32_t zpatch_popuplights_models[512] = {};
    uint32_t zpatch_wheelangle_models[512] = {};
    uint32_t zpatch_wheelangle_angles[512] = {};
}

namespace
{
    constexpr uint32_t kVehicleLightSlots = 512;

    // dword_10159C0C's call shape: __cdecl, the name hash then the index
    // out-parameter. A null slot faults, as the original's indirect call does.
    int LookupModel(uint32_t nameHash, int32_t* index)
    {
        return reinterpret_cast<int(__cdecl*)(uint32_t, int32_t*)>(zpatch_model_lookup)(nameHash, index);
    }

    // sub_10003E50 (source/shared.ixx), the original's own name resolver/hasher.
    // The checks call it rather than a C++ hash, so the "hash:" and quoted forms
    // resolve exactly as in the original.
    uint32_t ResolveModelName(const char* name)
    {
        return reinterpret_cast<uint32_t(__cdecl*)(const char*)>(Shared::ValueResolver())(name);
    }

    // _strnicmp(text, prefix, length) with the CRT's stop-at-NUL rule.
    bool CaseInsensitivePrefix(const char* text, const char* prefix, size_t length)
    {
        for (size_t i = 0; i < length; ++i)
        {
            unsigned char left = static_cast<unsigned char>(text[i]);
            unsigned char right = static_cast<unsigned char>(prefix[i]);
            if (left == 0 || right == 0)
                return left == right;
            if (left >= 'A' && left <= 'Z')
                left = static_cast<unsigned char>(left + ('a' - 'A'));
            if (right >= 'A' && right <= 'Z')
                right = static_cast<unsigned char>(right + ('a' - 'A'));
            if (left != right)
                return false;
        }
        return true;
    }

    // sub_10005F70 (191 instructions), rebuilt: the first siren check. The
    // thirteen built-in police model names are resolved and looked up in the
    // original's order; a match sets the flag. Otherwise the +siren list the IDE
    // parser filled is walked the same way. Every lookup is unguarded, as the
    // original's `call dword_10159C0C` is.
    void CopSirensCheckA()
    {
        constexpr const char* kPoliceModels[] = {
            "police", "police2", "police3", "police4", "noose", "pstockade",
            "nstockade", "polpatriot", "fbi", "n_police", "n_police2",
            "n_polpatriot", "n_pstockade",
        };
        const int32_t index = static_cast<int32_t>(zpatch_copsirens_index);
        zpatch_copsirens_flag = 0;
        for (const char* name : kPoliceModels)
        {
            int32_t found = -1;
            LookupModel(ResolveModelName(name), &found);
            if (found == index)
            {
                zpatch_copsirens_flag = 1;
                return;
            }
        }
        for (uint32_t slot = 0; slot < kVehicleLightSlots; ++slot)
        {
            const uint32_t model = zpatch_copsirens_models[slot];
            if (model == 0)
                break;
            int32_t found = -1;
            LookupModel(model, &found);
            if (found == index)
                zpatch_copsirens_flag = 1;
        }
    }

    // sub_10006260 (53 instructions), rebuilt: "police2" always matches; every
    // other model goes through the +siren list.
    void CopSirensCheckB()
    {
        const int32_t index = static_cast<int32_t>(zpatch_copsirens_index_b);
        zpatch_copsirens_flag_b = 0;
        int32_t found = -1;
        LookupModel(ResolveModelName("police2"), &found);
        if (found != index)
        {
            for (uint32_t slot = 0; slot < kVehicleLightSlots; ++slot)
            {
                const uint32_t model = zpatch_copsirens_models[slot];
                if (model == 0)
                    return;
                found = -1;
                LookupModel(model, &found);
                if (found == index)
                {
                    zpatch_copsirens_flag_b = 1;
                    return;
                }
            }
            return;
        }
        zpatch_copsirens_flag_b = 1;
    }

    // Appends the model's name hash to a 512-slot IDE table, deduplicating; a full
    // table stores nothing, as the original's `result > 0x7FC` return does.
    void AppendModel(uint32_t* slots, uint32_t model)
    {
        for (uint32_t slot = 0; slot < kVehicleLightSlots; ++slot)
        {
            if (slots[slot] == model)
                return;
            if (slots[slot] == 0)
            {
                slots[slot] = model;
                return;
            }
        }
    }

    // sub_10003FA0 (92 instructions), rebuilt: the IDE parser hook's payload. The
    // three arms are independent `if`s on the same string (the popuplights block
    // falls through to the siren check). The object read is unguarded, as the
    // original's `dword_1015BE0C + 60` is.
    void IdeParse()
    {
        const auto* text = reinterpret_cast<const char*>(static_cast<uintptr_t>(zpatch_ide_string));
        const auto* object = reinterpret_cast<const std::byte*>(static_cast<uintptr_t>(zpatch_ide_model));
        const uint32_t model = *reinterpret_cast<const uint32_t*>(object + 60);

        if (std::strcmp(text, "popuplights") == 0)
            AppendModel(zpatch_popuplights_models, model);
        if (std::strcmp(text, "siren") == 0)
            AppendModel(zpatch_copsirens_models, model);
        if (CaseInsensitivePrefix(text, "wheelangle:", 11))
        {
            const float angle = std::strtof(text + 11, nullptr);
            uint32_t bits = 0;
            std::memcpy(&bits, &angle, sizeof(bits));
            for (uint32_t slot = 0; slot < kVehicleLightSlots; ++slot)
            {
                if (zpatch_wheelangle_models[slot] == model)
                    return;
                if (zpatch_wheelangle_models[slot] == 0)
                {
                    zpatch_wheelangle_models[slot] = model;
                    zpatch_wheelangle_angles[slot] = bits;
                    return;
                }
            }
        }
    }
}

// sub_10006310, recovered VA 0x10006310 (8 instructions): the first siren shim.
// It stores the site's model index (EDX), runs the check between pusha/pushfd and
// popa/popfd, and resumes with EAX = the flag, which the site's `shl al, 6;
// xor al, [esi+0F6Fh]` continues on. pusha/popa are emitted as bytes: the inline
// assembler renders both with a 0x66 operand-size prefix.
static __declspec(naked) void CopSirensShimABody()
{
    __asm
    {
        mov DWORD PTR [zpatch_copsirens_index], edx
        __emit 0x60  // pusha
        __emit 0x9C  // pushfd
        call CopSirensCheckA
        __emit 0x9D  // popfd
        __emit 0x61  // popa
        mov eax, DWORD PTR [zpatch_copsirens_flag]
        jmp DWORD PTR [zpatch_copsirens_resume]
    }
}

// sub_10006330, recovered VA 0x10006330 (8 instructions): the second siren shim.
// It is installed at the anchor's +22 over the first five bytes of a six-byte
// `cmp`, and its own `cmp [flag], 1` is what the resumed `je` at the anchor's +28
// consumes; the replaced comparison's sixth byte is dead.
static __declspec(naked) void CopSirensShimBBody()
{
    __asm
    {
        mov DWORD PTR [zpatch_copsirens_index_b], eax
        __emit 0x60  // pusha
        __emit 0x9C  // pushfd
        call CopSirensCheckB
        __emit 0x9D  // popfd
        __emit 0x61  // popa
        cmp DWORD PTR [zpatch_copsirens_flag_b], 1
        jmp DWORD PTR [zpatch_copsirens_resume_b]
    }
}

// sub_100040E0, recovered VA 0x100040E0 (9 instructions): the shared IDE hook.
// After the payload it replays the site's `mov ecx,7; xor edx,edx; repe cmpsb`
// (the in-place copies are skipped by the resume at the anchor's -1), so the
// resumed `jne` still sees the compare's flags. `repe cmpsb` is emitted as bytes
// so the assembler cannot choose another form.
static __declspec(naked) void IdeHookBody()
{
    __asm
    {
        __emit 0x60  // pusha
        mov DWORD PTR [zpatch_ide_string], edi
        mov DWORD PTR [zpatch_ide_model], eax
        call IdeParse
        __emit 0x61  // popa
        mov ecx, 7
        xor edx, edx
        __emit 0xF3
        __emit 0xA6  // repe cmpsb
        jmp DWORD PTR [zpatch_ide_hook_slot]
    }
}

namespace
{
    // CopSirensFlag's three sites: the two model checks and the shared IDE hook,
    // installed at the parser anchor's -10 (the seven-byte compare).
    void PublishCopSirensFlag()
    {
        if (!Loader::Enabled("CopSirensFlag"))
            return;

        auto pattern = find_pattern("E8 ? ? ? ? C0 E0 ? 32 86 6F 0F 00");
        if (!pattern.empty())
            zpatch_copsirens_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(5)));

        pattern = find_pattern("0F BF 41 2E 39 05");
        if (!pattern.empty())
            zpatch_copsirens_resume_b = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(28)));

        pattern = find_pattern("0C 83 88 94 00 00 00 04");
        if (!pattern.empty())
            zpatch_ide_hook_slot = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(-1)));
    }

    void InstallCopSirensFlag()
    {
        if (!Loader::Enabled("CopSirensFlag"))
            return;

        auto pattern = find_pattern("E8 ? ? ? ? C0 E0 ? 32 86 6F 0F 00");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&CopSirensShimABody), true);

        pattern = find_pattern("0F BF 41 2E 39 05");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(22),
                reinterpret_cast<void*>(&CopSirensShimBBody), true);

        pattern = find_pattern("0C 83 88 94 00 00 00 04");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(-10),
                reinterpret_cast<void*>(&IdeHookBody), true);
    }
}

// PopupHeadlightsFlag: the checkbox that makes a vehicle's pop-up headlights
// deserve their own light cone. The two shims (sub_10044530 and its EDX twin
// sub_10044560) run the popuplights checker (sub_10044310) and continue into the
// site's own headlight test.
extern "C"
{
    // dword_1015BE20 (the site's model word), dword_1015BE24 (the check's flag),
    // dword_1015BE1C (the popuplights model index), dword_101580CC (the angle the
    // site's `fld` loads), and dword_1015D628 / dword_1015D62C (the two resumes).
    uint32_t zpatch_popup_model = 0;
    uint32_t zpatch_popup_flag = 0;
    uint32_t zpatch_popup_index = 0;
    float zpatch_popup_angle = 1.57079632679489661923f;  // pi/2, dword_101580CC
    uint32_t zpatch_popup_resume_a = 0;
    uint32_t zpatch_popup_resume_b = 0;

    // The two 512-slot [PopupLightAngles] tables the checker walks
    // (dword_1015C628 / dword_1015BE28) are Config::PopupLightAngles(), filled by
    // the loader preamble's reader. When the section is empty the hashes are all
    // zero, so the checker keeps the pi/2 above -- the original's own default.
}

namespace
{
    // sub_10044310 (125 instructions), rebuilt: reset the angle to pi/2, ask the
    // model store for the model the original names by the literal hash 0xEB9F21D3
    // (`push 0EB9F21D3h` at 0x1004432E) and compare the model against it, then walk
    // the +popuplights list the IDE parser filled. On a match the angle is the
    // last matching [PopupLightAngles] row, as in the original's eight interleaved
    // 64-entry columns (flattened here to 512 slots). The lookup is unguarded.
    void PopupHeadlightsCheck()
    {
        constexpr uint32_t kPopupLightsHash = 0xEB9F21D3u;

        zpatch_popup_angle = 1.57079632679489661923f;  // 0x3FC90FDB
        zpatch_popup_flag = 0;

        int32_t found = -1;
        LookupModel(kPopupLightsHash, &found);
        zpatch_popup_index = static_cast<uint32_t>(found);
        if (found == static_cast<int32_t>(zpatch_popup_model))
        {
            zpatch_popup_flag = 1;
            return;
        }

        if (zpatch_popuplights_models[0] == 0)
            return;

        uint32_t slot = 0;
        for (;;)
        {
            found = -1;
            LookupModel(zpatch_popuplights_models[slot], &found);
            if (found == static_cast<int32_t>(zpatch_popup_model))
                break;
            ++slot;
            if (slot >= kVehicleLightSlots || zpatch_popuplights_models[slot] == 0)
                return;
        }

        zpatch_popup_flag = 1;
        // The original scans exactly 512 slots (eight 64-entry columns), no matter
        // how many the section filled, and keeps the last match.
        const auto& table = Config::PopupLightAngles();
        for (uint32_t i = 0; i < kVehicleLightSlots; ++i)
        {
            if (table.hashes[i] != 0 &&
                table.hashes[i] == zpatch_popuplights_models[slot])
                zpatch_popup_angle = table.values[i];
        }
    }
}

// sub_10044530, recovered VA 0x10044530 (10 instructions): the EAX site's shim.
// It stores the site's model word, runs the check between pusha/popa, then takes
// the site's second branch when the model is a popuplights model, or falls into
// the anchor's +10 short jump (which the arm rewrites to unconditional) with the
// site's own `cmp eax, [index]` replayed first. pusha/popa are emitted as bytes
// because the inline assembler adds a 0x66 prefix.
static __declspec(naked) void PopupHeadlightsBodyA()
{
    __asm
    {
        movsx eax, WORD PTR [esi + 2Eh]
        __emit 0x60  // pusha
        mov DWORD PTR [zpatch_popup_model], eax
        call PopupHeadlightsCheck
        __emit 0x61  // popa
        cmp DWORD PTR [zpatch_popup_flag], 1
        je popup_body_a_resume_a
        cmp eax, DWORD PTR [zpatch_popup_index]
        jmp DWORD PTR [zpatch_popup_resume_b]
    popup_body_a_resume_a:
        jmp DWORD PTR [zpatch_popup_resume_a]
    }
}

// sub_10044560, recovered VA 0x10044560 (10 instructions): the EDX twin for the
// second site pattern, absent on 1.0.8.0 but present on the builds the key was
// gated for.
static __declspec(naked) void PopupHeadlightsBodyB()
{
    __asm
    {
        movsx edx, WORD PTR [esi + 2Eh]
        __emit 0x60  // pusha
        mov DWORD PTR [zpatch_popup_model], edx
        call PopupHeadlightsCheck
        __emit 0x61  // popa
        cmp DWORD PTR [zpatch_popup_flag], 1
        je popup_body_b_resume_a
        cmp edx, DWORD PTR [zpatch_popup_index]
        jmp DWORD PTR [zpatch_popup_resume_b]
    popup_body_b_resume_a:
        jmp DWORD PTR [zpatch_popup_resume_a]
    }
}

namespace
{
    // PopupHeadlightsFlag's sites: the EAX anchor (jump, its +10 branch forced to
    // a short jump, and the `fld` operand at +82 retargeted to our pi/2 float),
    // the shared IDE hook, and the EDX twin when the image has it.
    void PublishPopupHeadlightsFlag()
    {
        if (!Loader::Enabled("PopupHeadlightsFlag"))
            return;

        auto pattern = find_pattern("0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1");
        if (!pattern.empty())
        {
            zpatch_popup_resume_b = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(10)));
            zpatch_popup_resume_a = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(12)));
        }

        pattern = find_pattern("0C 83 88 94 00 00 00 04");
        if (!pattern.empty())
            zpatch_ide_hook_slot = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(-1)));

        pattern = find_pattern("0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1");
        if (!pattern.empty())
        {
            zpatch_popup_resume_b = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(10)));
            zpatch_popup_resume_a = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(12)));
        }
    }

    void InstallPopupHeadlightsFlag()
    {
        if (!Loader::Enabled("PopupHeadlightsFlag"))
            return;

        auto pattern = find_pattern("0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            injector::MakeJMP(match, reinterpret_cast<void*>(&PopupHeadlightsBodyA), true);
            injector::WriteMemory<std::byte>(match + 10, Bytes::ShortJmp, true);
            injector::WriteMemory<uint32_t>(match + 82,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_popup_angle)), true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(match + 82));
        }

        pattern = find_pattern("0C 83 88 94 00 00 00 04");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(-10),
                reinterpret_cast<void*>(&IdeHookBody), true);

        pattern = find_pattern("0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            injector::MakeJMP(match, reinterpret_cast<void*>(&PopupHeadlightsBodyB), true);
            injector::WriteMemory<std::byte>(match + 10, Bytes::ShortJmp, true);
            injector::WriteMemory<uint32_t>(match + 82,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_popup_angle)), true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(match + 82));
        }
    }
}

// Registered like every other arm: the publish tier reads the site's original
// bytes, the install tier owns the gate.
export void RegisterMisc()
{
    Loader::PublishEvent() += []
    {
        PublishDrivebyFOVFix();
        PublishSpeedupBugFix();
        PublishPatch4RemoveModChecks();
        PublishCopSirensFlag();
        PublishPopupHeadlightsFlag();
    };

    Loader::InstallEvent() += []
    {
        InstallMiscFixes();
        InstallPatch6and7RemoveModChecks();
        InstallDrivebyFOVFix();
        InstallSpeedupBugFix();
        InstallPatch4RemoveModChecks();
        InstallCopSirensFlag();
        InstallPopupHeadlightsFlag();
    };

    // The bodies' recovered VAs, so the dump expresses each arm's branch in the
    // original's coordinates.
    Bodies::Register(reinterpret_cast<uintptr_t>(&SpeedupBugFixBody), 0x10050540u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&Patch4StubBody), 0x100054E0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&CopSirensShimABody), 0x10006310u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&CopSirensShimBBody), 0x10006330u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&IdeHookBody), 0x100040E0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&PopupHeadlightsBodyA), 0x10044530u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&PopupHeadlightsBodyB), 0x10044560u);

    // Each stub's inline `call` is invisible to C++; naming the helper here keeps
    // its out-of-line definition in the image.
    [[maybe_unused]] const uintptr_t helpers[] = {
        reinterpret_cast<uintptr_t>(&Patch4Step),
        reinterpret_cast<uintptr_t>(&CopSirensCheckA),
        reinterpret_cast<uintptr_t>(&CopSirensCheckB),
        reinterpret_cast<uintptr_t>(&IdeParse),
        reinterpret_cast<uintptr_t>(&PopupHeadlightsCheck),
    };
}

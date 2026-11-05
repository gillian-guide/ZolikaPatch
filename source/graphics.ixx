module;

#include <common.hxx>

export module graphics;

import bodies;
import bytes;
import common;
import loader;
import patch;

// The two published model hashes and the two resumes the shim reads. C linkage:
// the transcribed asm names them directly.
extern "C"
{
    uint32_t zpatch_bds_hash_a = 0;
    uint32_t zpatch_bds_hash_b = 0;
    void* zpatch_bds_resume_a = nullptr;
    void* zpatch_bds_resume_b = nullptr;
}

// abi_building_dynamic_shadows, recovered VA 0x10005AF0 (sub_10005af0). It reads
// the model pointer the caller holds in EDI, matches it against the two published
// hashes, and either takes the special path or rewinds the packed field by one.
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails.
static __declspec(naked) void BuildingsDynamicShadowsShim()
{
    __asm
    {
        mov eax, DWORD PTR [edi]
        cmp eax, DWORD PTR [zpatch_bds_hash_a]
        je bds_special
        cmp eax, DWORD PTR [zpatch_bds_hash_b]
        je bds_special
        mov eax, DWORD PTR [edi + 0x28]
        shr eax, 6
        and eax, 0x0f
        add eax, -1
        jmp DWORD PTR [zpatch_bds_resume_a]
    bds_special:
        jmp DWORD PTR [zpatch_bds_resume_b]
    }
}

namespace
{
    // abi_aim_projectile_crash_fix, recovered VA 0x10005590 (sub_10005590): a
    // __thiscall that stores the caller's two dwords into the object in ECX, derives
    // a flag from the second and returns with `ret 12`. AimProjectileCrashFix jumps
    // here; the body is hosted in this module because a naked definition is only
    // accepted here, and it is handed out through the accessor below.
    __declspec(naked) void AimProjectileShim()
    {
        __asm
        {
            cmp ecx, 0
            je aim_done
            mov eax, DWORD PTR [esp + 8]
            mov DWORD PTR [ecx + 0x20], eax
            mov eax, DWORD PTR [esp + 0xc]
            mov DWORD PTR [ecx + 0x24], eax
            neg eax
            sbb eax, eax
            and eax, DWORD PTR [esp + 4]
            mov DWORD PTR [ecx + 0x1c], eax
        aim_done:
            ret 12
        }
    }
}

// The aim shim's address, registered at its recovered VA on first use.
export void* AimProjectileShimAddress()
{
    Bodies::Register(reinterpret_cast<uintptr_t>(&AimProjectileShim), 0x10005590u);
    return reinterpret_cast<void*>(&AimProjectileShim);
}

// The EmissiveLerpFix pair's published state. The original keeps it in four .bss
// words beside its own code (dword_10159CC0/CCC/CC4/CC8); the transcribed asm
// names the slots directly, so they carry C linkage.
extern "C"
{
    uint32_t zpatch_emissive_saved_ecx = 0;  // dword_10159CC0
    uint32_t zpatch_emissive_saved_edx = 0;  // dword_10159CCC
    uint32_t zpatch_emissive_scale = 0;      // dword_10159CC4 (match - 16)
    uint32_t zpatch_emissive_resume = 0;     // dword_10159CC8 (match - 12)
}

// sub_1001AF20, recovered VA 0x1001AF20: the body sub_1001AF60 calls. At the
// 1.0.8.0 site (0x7F8BA1) the shim saves ECX = the clock's hours and EDX = its
// minutes. `shl ecx, 4; sub ecx, [saved_ecx]` leaves 15 * hours, and
// `lea eax, [eax + ecx * 4]` adds 4 * 15 * hours to the minutes, i.e.
// 60 * hours + minutes = the minute of the day. 0xFFFFFE6B (-405) is 06:45 and
// 0x2ED (753) is twelve hours and thirty-three minutes, so the unsigned compare
// selects 12 inside [06:45, 19:18] and 0 outside it. The selector is written over
// the saved ECX, and the saved EDX is zeroed; the shim reloads both, so the
// resumed code takes ECX as hours and computes the day/night factor from noon or
// midnight with no minutes at all.
static __declspec(naked) void EmissiveLerpHelper()
{
    __asm
    {
        mov ecx, DWORD PTR [zpatch_emissive_saved_ecx]
        xor edx, edx
        mov eax, DWORD PTR [zpatch_emissive_saved_edx]
        shl ecx, 4
        sub ecx, DWORD PTR [zpatch_emissive_saved_ecx]
        mov DWORD PTR [zpatch_emissive_saved_edx], 0
        lea eax, [eax + ecx * 4]
        mov ecx, 0Ch
        add eax, -405
        cmp eax, 2EDh
        cmovbe edx, ecx
        mov DWORD PTR [zpatch_emissive_saved_ecx], edx
        ret
    }
}

// sub_1001AF60, recovered VA 0x1001AF60: the trampoline the EmissiveLerpFix arm
// installs at match - 24. The original handler sub_1001AFA0 publishes two
// addresses before it writes the site: match - 16 into dword_10159CC4 and
// match - 12 into dword_10159CC8. Both slots hold addresses, and the shim consumes
// them differently:
//   * dword_10159CC8 is the resume (0x7F8BAD on 1.0.8.0); the final indirect
//     `jmp dword ptr [slot]` takes the slot's value as the jump target;
//   * dword_10159CC4 is match - 16 (0x7F8BA9), the address of the disp32 of the
//     `mulss xmm0, 60.0f` being replaced (the four bytes there are 0x00DF2C30,
//     the address of the game's 60.0f). The mulss is IDA's ABSOLUTE operand:
//     `F3 0F 59 05 <disp32>` reads the slot's own dword as the float, with no
//     register load and no dereference. So the arm publishes match - 16, not a
//     float copied out of the game.
// The multiply has no effect on the result: the helper zeroed the saved EDX, so
// `cvtsi2ss xmm0, edx` produces 0.0f before it runs. The original keeps the
// multiply and the slot write anyway, so this body does too.
//
// The 0x3E before the mulss is the original's DS segment prefix (the byte at
// 0x1001AF83, not an instruction of its own). `pusha`/`popa` are emitted as
// bytes because the inline assembler renders those mnemonics with a 0x66
// operand-size prefix, which the original does not have.
static __declspec(naked) void EmissiveLerpShim()
{
    __asm
    {
        mov DWORD PTR [zpatch_emissive_saved_ecx], ecx
        mov DWORD PTR [zpatch_emissive_saved_edx], edx
        __emit 0x60                                  // pusha
        call EmissiveLerpHelper
        __emit 0x61                                  // popa
        mov ecx, DWORD PTR [zpatch_emissive_saved_ecx]
        mov edx, DWORD PTR [zpatch_emissive_saved_edx]
        cvtsi2ss xmm0, edx
        __emit 0x3E                                  // DS prefix, as in the original
        mulss xmm0, DWORD PTR [zpatch_emissive_scale]
        jmp DWORD PTR [zpatch_emissive_resume]
    }
}

// The emissive shim's address, registered at its recovered VA on first use.
export void* EmissiveLerpShimAddress()
{
    Bodies::Register(reinterpret_cast<uintptr_t>(&EmissiveLerpShim), 0x1001AF60u);
    return reinterpret_cast<void*>(&EmissiveLerpShim);
}

namespace
{
    // BuildingDynamicShadows: the shadow-size operand at the call site becomes zero
    // (0x104 -> 0x100) and the second site jumps into the shim. The published words
    // go in first: both resumes come from the site's original bytes, so publishing
    // after the write would resume into the jump itself.
    void InstallBuildingDynamicShadows()
    {
        if (!Loader::Enabled("BuildingDynamicShadows"))
            return;

        auto pattern = find_pattern("50 68 04 01 00 00 6A 02");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(2), Bytes::Zero, true);

        pattern = find_pattern("8B 47 ? C1 E8 06 83 E0 0F 83 C0 FF");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        zpatch_bds_resume_a = match + 15;
        zpatch_bds_resume_b = match + 112;

        // The hash slots take the same field of the first and of the second match.
        pattern = find_pattern("8B 46 28 83 4E 24 05 25 7F FC FF FF 83 C8 40 89 46 28 C7 06");
        if (!pattern.empty())
            zpatch_bds_hash_a = *pattern.get(0).get<uint32_t>(20);
        if (pattern.size() > 1)
            zpatch_bds_hash_b = *pattern.get(1).get<uint32_t>(20);

        Bodies::Register(reinterpret_cast<uintptr_t>(&BuildingsDynamicShadowsShim), 0x10005AF0u);
        injector::MakeJMP(match, reinterpret_cast<void*>(&BuildingsDynamicShadowsShim), true);
    }

    // EmissiveLerpFix: the original handler sub_1001AFA0 scans this signature, then
    // publishes match - 12 as the resume and match - 16 as the float slot before it
    // writes the E9 at match - 24. Both published values are addresses, not values
    // read out of the game (see EmissiveLerpShim); the order is kept because the
    // original publishes before it patches.
    void InstallEmissiveLerpFix()
    {
        if (!Loader::Enabled("EmissiveLerpFix"))
            return;

        auto pattern = find_pattern("F3 0F 58 C1 F3 0F 2A C8 F3 0F 58 C1");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        zpatch_emissive_scale = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match - 16));
        zpatch_emissive_resume = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match - 12));

        injector::MakeJMP(match - 24, EmissiveLerpShimAddress(), true);
    }
}

// Registered like every other arm: the install tier owns the gate.
export void RegisterGraphics()
{
    Loader::InstallEvent() += [] { InstallBuildingDynamicShadows(); };
    Loader::InstallEvent() += [] { InstallEmissiveLerpFix(); };
}

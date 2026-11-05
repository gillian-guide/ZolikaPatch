module;

#include <common.hxx>

export module graphics;

import bodies;
import bytes;
import common;
import loader;
import pattern;

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

        auto operand = Pattern("50 68 04 01 00 00 6A 02 6A 00 E8");
        if (operand.size() > 0)
            injector::WriteMemory<std::byte>(MatchAddress(operand, 2), Bytes::Zero, true);

        auto site = Pattern("8B 47 28 C1 E8 06 83 E0 0F 83 C0");
        if (site.empty())
            return;

        auto* match = MatchAddress(site, 0);
        zpatch_bds_resume_a = match + 15;
        zpatch_bds_resume_b = match + 112;

        // The hash slots take the same field of the first and of the second match.
        auto hashes = Pattern("8B 46 28 83 4E 24 05 25 7F FC FF FF 83 C8 40 89 46 28 C7 06");
        if (hashes.size() > 0)
            zpatch_bds_hash_a = *MatchAddress<uint32_t>(hashes, 20);
        if (hashes.size() > 1)
            zpatch_bds_hash_b = *hashes.get(1).get<uint32_t>(20);

        Bodies::Register(reinterpret_cast<uintptr_t>(&BuildingsDynamicShadowsShim), 0x10005AF0u);
        injector::MakeJMP(match, reinterpret_cast<void*>(&BuildingsDynamicShadowsShim), true);
    }
}

// Registered like every other arm: the install tier owns the gate.
export void RegisterGraphics()
{
    Loader::InstallEvent() += [] { InstallBuildingDynamicShadows(); };
}

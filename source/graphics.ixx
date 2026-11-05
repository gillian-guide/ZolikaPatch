module;

#include <common.hxx>

export module graphics;

import bodies;
import bytes;
import common;
import game;
import loader;
import patch;

// The two published model hashes and the two resumes the shim reads. C linkage:
// the transcribed asm names them directly.
extern "C"
{
    uint32_t zpatch_bds_hash_a = 0;              // dword_10159C1C
    uint32_t zpatch_bds_hash_b = 0;              // dword_10159C24
    void* zpatch_bds_resume_a = nullptr;         // dword_10159C20
    void* zpatch_bds_resume_b = nullptr;         // dword_10159C28
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

// The SuperLODFix pair's published state: the two resumes the shim tails into
// (dword_101580AC and dword_101580B0) and the lookup the model helper calls
// (dword_101580B4). The original keeps them in .bss beside its code; the
// transcribed asm names the slots directly, so they carry C linkage.
extern "C"
{
    uint32_t zpatch_super_lod_fail = 0;    // dword_101580AC
    uint32_t zpatch_super_lod_ok = 0;      // dword_101580B0
    uint32_t zpatch_super_lod_lookup = 0;  // dword_101580B4
}

// sub_100469A0, recovered VA 0x100469A0: the model test the shim calls with the
// ESI argument still on the stack. It resolves the constant 0xAE4B15D6 through
// the published lookup and compares the object's signed word at +0x2E with the
// answer. The frame is the body's own /GS frame: the cookie word and the cookie
// check are the module's, exactly as a compiled function of this module has
// them, and both are absolute/slot operands in the proof.
static __declspec(naked) void SuperLodModel()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 8
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        lea eax, [ebp - 8]
        push eax
        push 0AE4B15D6h
        call DWORD PTR [zpatch_super_lod_lookup]
        mov eax, DWORD PTR [ebp + 8]
        add esp, 8
        mov ecx, DWORD PTR [ebp - 4]
        movsx eax, WORD PTR [eax + 2Eh]
        cmp eax, DWORD PTR [ebp - 8]
        setz al
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_100469E0, recovered VA 0x100469E0: the body the SuperLODFix arm installs at
// the `test byte [eax+72h], 10h` / `jz` pair. EAX is the object and ESI the model
// the test reads. A model that resolves to the published hash continues at the
// site's continuation (ok); otherwise the object's 0x10 flag decides, and the
// `jz` it would have taken is the site's branch target (fail).
static __declspec(naked) void SuperLodShim()
{
    __asm
    {
        jmp slod_main
    slod_fail:
        jmp DWORD PTR [zpatch_super_lod_fail]
    slod_main:
        push eax
        push ecx
        push esi
        call SuperLodModel
        add esp, 4
        pop ecx
        test al, al
        pop eax
        jnz slod_ok
        test byte ptr [eax + 72h], 10h
        jz slod_fail
    slod_ok:
        jmp DWORD PTR [zpatch_super_lod_ok]
    }
}

// sub_100459E0, recovered VA 0x100459E0: the HPE filter the shader finder calls
// with the shader-name object. The finder takes the entry whose filter result is 0,
// so 0 means "this is the name kind we want": the kind byte 'h', or 'p' followed by
// 'e'. Every other name returns 2, which the finder reads as "keep looking".
static __declspec(naked) void ShaderStreamingFilterHpe()
{
    __asm
    {
        push ebp
        mov ebp, esp
        mov eax, DWORD PTR [ebp + 0Ch]
        mov cl, BYTE PTR [eax + 4]
        cmp cl, 68h
        jz hpe_match
        cmp cl, 70h
        jnz hpe_skip
        cmp BYTE PTR [eax + 5], 65h
        jz hpe_match
    hpe_skip:
        mov eax, 2
        pop ebp
        ret
    hpe_match:
        xor eax, eax
        pop ebp
        ret
    }
}

// sub_10045A10, recovered VA 0x10045A10: the V filter, the same 0-for-match /
// 2-for-skip contract for the single kind byte 'v'.
static __declspec(naked) void ShaderStreamingFilterV()
{
    __asm
    {
        push ebp
        mov ebp, esp
        mov ecx, DWORD PTR [ebp + 0Ch]
        xor edx, edx
        mov eax, 2
        cmp BYTE PTR [ecx + 4], 76h
        cmovz eax, edx
        pop ebp
        ret
    }
}

// sub_10045A30, recovered VA 0x10045A30: the replacement for the
// `LookupShader("gta_ped")` call (the arm's second write then jumps over the
// following ped-name lookups). It walks the shader group's array (count at +0Ch,
// array at +8), asks each shader's virtual +0x10 for its name object and takes
// the first one the HPE filter reports as the wanted kind (a 0 result; 2 means
// keep looking), or returns null. `ret 4` because the caller pushed two arguments.
static __declspec(naked) void ShaderStreamingFindHpe()
{
    __asm
    {
        push ebx
        push esi
        push edi
        mov edi, ecx
        xor esi, esi
        cmp WORD PTR [edi + 0Ch], si
        jbe hpe_none
        mov ebx, DWORD PTR [esp + 10h]
    hpe_loop:
        mov eax, DWORD PTR [edi + 8]
        mov ecx, DWORD PTR [eax + esi * 4]
        test ecx, ecx
        jz hpe_advance
        mov edx, DWORD PTR [ecx]
        mov eax, DWORD PTR [edx + 10h]
        call eax
        push eax
        push ebx
        call ShaderStreamingFilterHpe
        add esp, 8
        test eax, eax
        jz hpe_found
    hpe_advance:
        movzx ecx, WORD PTR [edi + 0Ch]
        add esi, 1
        cmp esi, ecx
        jl hpe_loop
    hpe_none:
        pop edi
        pop esi
        xor eax, eax
        pop ebx
        ret 4
    hpe_found:
        mov edx, DWORD PTR [edi + 8]
        mov eax, DWORD PTR [edx + esi * 4]
        pop edi
        pop esi
        pop ebx
        ret 4
    }
}

// sub_10045A80, recovered VA 0x10045A80: the same walk for the V filter, the
// replacement for the `LookupShader("gta_vehicle_default")` call (the arm's fourth
// write then jumps to the end of the shader chain).
static __declspec(naked) void ShaderStreamingFindV()
{
    __asm
    {
        push ebx
        push esi
        push edi
        mov edi, ecx
        xor esi, esi
        cmp WORD PTR [edi + 0Ch], si
        jbe v_none
        mov ebx, DWORD PTR [esp + 10h]
    v_loop:
        mov eax, DWORD PTR [edi + 8]
        mov ecx, DWORD PTR [eax + esi * 4]
        test ecx, ecx
        jz v_advance
        mov edx, DWORD PTR [ecx]
        mov eax, DWORD PTR [edx + 10h]
        call eax
        push eax
        push ebx
        call ShaderStreamingFilterV
        add esp, 8
        test eax, eax
        jz v_found
    v_advance:
        movzx ecx, WORD PTR [edi + 0Ch]
        add esi, 1
        cmp esi, ecx
        jl v_loop
    v_none:
        pop edi
        pop esi
        xor eax, eax
        pop ebx
        ret 4
    v_found:
        mov edx, DWORD PTR [edi + 8]
        mov eax, DWORD PTR [edx + esi * 4]
        pop edi
        pop esi
        pop ebx
        ret 4
    }
}

namespace
{
    // BuildingAlphaFix: two copies of the `test [esi+24h], 40000000h` alpha gate.
    // The first one's `jz` (0F 84 rel32, six bytes) becomes a five-byte `jmp` to
    // the same target, so the alpha path is entered unconditionally; the second
    // one's `jnz` and the five bytes of the test it follows are filled with NOPs,
    // so that copy never skips the path either. The targets are the sites' own
    // operands, read before the opcode is overwritten.
    void InstallBuildingAlphaFix()
    {
        if (!Loader::Enabled("BuildingAlphaFix"))
            return;

        auto pattern = find_pattern("F7 46 24 00 00 00 40 0F 84");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            const auto displacement = *reinterpret_cast<const int32_t*>(match + 9);
            injector::MakeJMP(match + 7, match + 13 + displacement, true);
        }

        pattern = find_pattern("F7 46 24 00 00 00 40 0F 85");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 13, true);
    }

    // RemoveBoundingBoxCulling: the `jnz` that skips the bounding-box path when the
    // object is not a building becomes an unconditional `jmp` to that same target,
    // i.e. the path is always skipped.
    void InstallRemoveBoundingBoxCulling()
    {
        if (!Loader::Enabled("RemoveBoundingBoxCulling"))
            return;

        auto pattern = find_pattern("3C 01 57 0F 85");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        const auto displacement = *reinterpret_cast<const int32_t*>(match + 5);
        injector::MakeJMP(match + 3, match + 9 + displacement, true);
    }

    // RemoveStaticCarShadows: the `jbe` (+0x5B) that guards the static car-shadow
    // draw becomes a short `jmp` over it, so the shadow is never drawn.
    void InstallRemoveStaticCarShadows()
    {
        if (!Loader::Enabled("RemoveStaticCarShadows"))
            return;

        auto pattern = find_pattern("F3 0F 11 4C 24 0C 76 5B D9 45 14");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(6), Bytes::ShortJmp, true);
    }

    // ForceShadowsOnObjects: the `jz` (+7) that would skip the shadow flag on an
    // object with the 0x100000 bit set becomes a short `jmp` over it, so the flag
    // is always set.
    void InstallForceShadowsOnObjects()
    {
        if (!Loader::Enabled("ForceShadowsOnObjects"))
            return;

        auto pattern = find_pattern("F7 47 40 00 10 00 00 74 07 81 4E 24 00 00");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(7), Bytes::ShortJmp, true);
    }

    // ForceDynamicShadowsEverywhere: the short `jz` that skips the dynamic-shadow
    // path becomes two NOPs, so the path always runs.
    void InstallForceDynamicShadowsEverywhere()
    {
        if (!Loader::Enabled("ForceDynamicShadowsEverywhere"))
            return;

        auto pattern = find_pattern("F7 46 4C 00 80 00 00 74");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(7), 2, true);
    }

    // BuildingDynamicShadows' published state: both resumes come from the site's
    // original bytes (match+15 and match+112) and the two hash slots take the same
    // field of the first and of the second scan hit.
    void PublishBuildingDynamicShadows()
    {
        if (!Loader::Enabled("BuildingDynamicShadows"))
            return;

        auto pattern = find_pattern("8B 47 ? C1 E8 06 83 E0 0F 83 C0 FF");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        zpatch_bds_resume_a = match + 15;
        zpatch_bds_resume_b = match + 112;

        // Two hits, one per model hash the shim compares against, so the scan has to
        // ask for both: the scanner's count is a hint that stops the scan, and the
        // default one hit would leave the second slot at zero.
        pattern = find_pattern<2>("8B 46 28 83 4E 24 05 25 7F FC FF FF 83 C8 40 89 46 28 C7 06");
        if (!pattern.empty())
            zpatch_bds_hash_a = *pattern.get(0).get<uint32_t>(20);
        if (pattern.size() > 1)
            zpatch_bds_hash_b = *pattern.get(1).get<uint32_t>(20);
    }

    // BuildingDynamicShadows: the shadow-size operand at the call site becomes zero
    // (0x104 -> 0x100) and the second site jumps into the shim.
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

        Bodies::Register(reinterpret_cast<uintptr_t>(&BuildingsDynamicShadowsShim), 0x10005AF0u);
        injector::MakeJMP(pattern.get_first(0),
            reinterpret_cast<void*>(&BuildingsDynamicShadowsShim), true);
    }

    // EmissiveLerpFix's published state: match - 12 as the resume and match - 16 as
    // the float slot. Both published values are addresses, not values read out of
    // the game (see EmissiveLerpShim).
    void PublishEmissiveLerpFix()
    {
        if (!Loader::Enabled("EmissiveLerpFix"))
            return;

        auto pattern = find_pattern("F3 0F 58 C1 F3 0F 2A C8 F3 0F 58 C1");
        if (pattern.empty())
            return;

        const auto match = reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0));
        zpatch_emissive_scale = static_cast<uint32_t>(match - 16);
        zpatch_emissive_resume = static_cast<uint32_t>(match - 12);
    }

    // EmissiveLerpFix: the original handler sub_1001AFA0 scans the signature and
    // writes the E9 at match - 24.
    void InstallEmissiveLerpFix()
    {
        if (!Loader::Enabled("EmissiveLerpFix"))
            return;

        auto pattern = find_pattern("F3 0F 58 C1 F3 0F 2A C8 F3 0F 58 C1");
        if (pattern.empty())
            return;

        injector::MakeJMP(pattern.get_first(-24), EmissiveLerpShimAddress(), true);
    }

    // The per-build set InstallSuperLODFix publishes: the site's two continuations
    // and the hash lookup the model helper calls, each a game RVA from the
    // recovered handler's own base + RVA pairs (the suffix is the build).
    struct SuperLodTargets
    {
        Build build;
        uint32_t fail;
        uint32_t ok;
        uint32_t lookup;
    };
    constexpr SuperLodTargets kSuperLodTargets[] = {
        { Build::v1070, 0x4A3D37u, 0x4A3CEFu, 0x58AAE0u },
        { Build::v1080, 0x540E67u, 0x540E1Fu, 0x4DD2D0u },
    };

    // SuperLODFix's published state: the site's two continuations and the hash lookup
    // the model helper calls, each a game RVA from the recovered handler's own base +
    // RVA pairs (the suffix is the build).
    void PublishSuperLODFix()
    {
        if (!Loader::Enabled("SuperLODFix"))
            return;

        auto pattern = find_pattern("F6 40 ? ? 74 ? 85 ED 0F 85 ? ? ? ? 55 6A ? E8 ? ? ? ? 8B F8");
        if (pattern.empty())
            return;

        const auto base = Game::Get().ImageBase();
        for (const SuperLodTargets& targets : kSuperLodTargets)
        {
            if (targets.build != Loader::Current())
                continue;
            zpatch_super_lod_fail = static_cast<uint32_t>(base + targets.fail);
            zpatch_super_lod_ok = static_cast<uint32_t>(base + targets.ok);
            zpatch_super_lod_lookup = static_cast<uint32_t>(base + targets.lookup);
        }
    }

    // SuperLODFix: the two-byte pair `test byte [eax+72h], 10h` / `jz` that decides
    // whether the detailed LOD is used is replaced by the model-hash test. The fail
    // path is the `jz` target, the ok path the instruction after the six-byte pair,
    // and the lookup is the game's hash function.
    void InstallSuperLODFix()
    {
        if (!Loader::Enabled("SuperLODFix"))
            return;

        auto pattern = find_pattern("F6 40 ? ? 74 ? 85 ED 0F 85 ? ? ? ? 55 6A ? E8 ? ? ? ? 8B F8");
        if (pattern.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&SuperLodModel), 0x100469A0u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SuperLodShim), 0x100469E0u);
        injector::MakeJMP(pattern.get_first(0), reinterpret_cast<void*>(&SuperLodShim), true);
    }

    // ImprovedShaderStreaming: the name-by-name LookupShader walks become one call
    // each into a shader-array walk with a kind filter, and the rest of the old
    // chain is jumped over. The original's handler sub_10045AD0 writes the four
    // sites at match + 0x55 (E8 body A), + 0x62 (E9, displacement 0x8E), + 0xFD
    // (E8 body B) and + 0x10A (E9, displacement 0x377); the displacement it writes
    // is the constant but it encodes exactly the two targets below, so the jumps
    // are computed rather than hardcoded. The finders are the bodies the calls
    // name; the filters are only called by the finders, so they appear in the body
    // manifest rather than as branch targets.
    void InstallImprovedShaderStreaming()
    {
        if (!Loader::Enabled("ImprovedShaderStreaming"))
            return;

        auto pattern = find_pattern(
            "33 F6 8B 4C 24 08 8B 06 8B 50 04 51 8B CE FF D2 F6 D8 1B C0 23 C6 5E C3 8B 44 24 0C 8B 74 24");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        Bodies::Register(reinterpret_cast<uintptr_t>(&ShaderStreamingFindHpe), 0x10045A30u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&ShaderStreamingFindV), 0x10045A80u);

        injector::MakeCALL(match + 85, reinterpret_cast<void*>(&ShaderStreamingFindHpe), true);
        injector::MakeJMP(match + 98, match + 245, true);
        injector::MakeCALL(match + 253, reinterpret_cast<void*>(&ShaderStreamingFindV), true);
        injector::MakeJMP(match + 266, match + 1158, true);
    }
}

// Registered like every other arm: the publish tier reads every site the install
// tier is about to overwrite, and the install tier owns the gate and the writes.
export void RegisterGraphics()
{
    Loader::PublishEvent() += []
    {
        PublishBuildingDynamicShadows();
        PublishEmissiveLerpFix();
        PublishSuperLODFix();
    };
    Loader::InstallEvent() += [] { InstallBuildingAlphaFix(); };
    Loader::InstallEvent() += [] { InstallBuildingDynamicShadows(); };
    Loader::InstallEvent() += [] { InstallEmissiveLerpFix(); };
    Loader::InstallEvent() += [] { InstallForceDynamicShadowsEverywhere(); };
    Loader::InstallEvent() += [] { InstallForceShadowsOnObjects(); };
    Loader::InstallEvent() += [] { InstallImprovedShaderStreaming(); };
    Loader::InstallEvent() += [] { InstallRemoveBoundingBoxCulling(); };
    Loader::InstallEvent() += [] { InstallRemoveStaticCarShadows(); };
    Loader::InstallEvent() += [] { InstallSuperLODFix(); };
}

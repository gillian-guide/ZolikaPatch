module;

#include <common.hxx>

export module vehicles;

import bodies;
import bytes;
import common;
import loader;
import patch;

// The car-parts shadow bodies' published operands: C linkage because the
// transcribed asm names them directly.
//   zpatch_carparts_resume_if / _else  dword_1015BDC8 / dword_1015BDC0, plus the
//                                      site's two resume addresses (match+2 and
//                                      match+0x45 of the "85 DB 7E 41 55 EB 0A"
//                                      anchor);
//   zpatch_carparts_resume_2          dword_1015BDCC, the second detour's resume;
//   zpatch_carparts_shadow_fn         dword_1015B5B8, the call target it reads.
extern "C"
{
    uint32_t zpatch_carparts_resume_if = 0;
    uint32_t zpatch_carparts_resume_else = 0;
    uint32_t zpatch_carparts_resume_2 = 0;
    uint32_t zpatch_carparts_shadow_fn = 0;

    // dword_1015BDD4 / dword_1015BDD8: the node-walk test the collision body calls
    // and the deadlock counter it compares.
    uint32_t zpatch_collision_should_walk = 0;
    uint32_t zpatch_collision_iterations = 0;
}

// abi_collision_deadlock_walk, recovered VA 0x10005D80: the whole node-walk of
// sub_4E6250 with an iteration cap in it. The walk is the game's own, instruction
// for instruction -- the only additions are the counter word (zeroed at entry,
// incremented per node, compared against 0x2710) and the early exit when it
// trips. The walk test is called through the published pointer (dword_1015BDD4)
// instead of a direct call, because the call's target is the site's own operand.
static __declspec(naked) void CollisionDeadlockShim()
{
    __asm
    {
        mov DWORD PTR [zpatch_collision_iterations], 0
        push ebp
        mov ebp, esp
        and esp, 0FFFFFFF8h
        push ecx
        mov eax, DWORD PTR [ebp + 0xC]
        movzx ecx, WORD PTR [eax + 0x66]
        push ebx
        mov ebx, DWORD PTR [ebp + 8]
        mov edx, DWORD PTR [ebx + 0x70]
        push esi
        push edi
        movzx edi, WORD PTR [edx + ecx * 8]
        cmp di, 0FFFFh
        jz collision_walk_done
    collision_walk_next:
        mov ecx, DWORD PTR [ebx + 0x70]
        movzx eax, di
        movzx edx, WORD PTR [ecx + eax * 8]
        lea esi, [ecx + eax * 8]
        mov eax, DWORD PTR [esi + 4]
        and eax, 0FFFFFFF0h
        cmp dx, 0FFFFh
        jz collision_walk_no_neighbour
        movzx edx, dx
        mov ecx, DWORD PTR [ecx + edx * 8 + 4]
        and ecx, 0FFFFFFF0h
        prefetcht0 BYTE PTR [ecx]
    collision_walk_no_neighbour:
        mov ecx, DWORD PTR [esi + 4]
        and ecx, 3
        mov edx, 1
        shl edx, cl
        push edx
        mov edx, DWORD PTR [ebp + 0xC]
        call DWORD PTR [zpatch_collision_should_walk]
        test al, al
        jnz collision_walk_found
        movzx edi, WORD PTR [esi]
        add DWORD PTR [zpatch_collision_iterations], 1
        cmp DWORD PTR [zpatch_collision_iterations], 2710h
        jz collision_walk_done
        cmp di, 0FFFFh
        jnz collision_walk_next
    collision_walk_done:
        or ax, 0FFFFh
        pop edi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret 8
    collision_walk_found:
        mov ax, di
        pop edi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret 8
    }
}

// abi_carparts_shadow, recovered VA 0x10006880: the shared car-parts shadow body.
// The site's `movzx ebx,[edi+1Ch]` / `xor esi,esi` are re-run here (the jump
// overwrites them), with a null guard on the parts pointer in front, and the
// re-run `test ebx,ebx` is what the resumed `jle` reads. The nops are the
// original's own padding between those instructions, not filler added here.
static __declspec(naked) void CarPartsShadowShim()
{
    __asm
    {
        cmp edi, 0
        nop
        jz carparts_shadow_skip
        nop
        movzx ebx, WORD PTR [edi + 0x1C]
        nop
        xor esi, esi
        nop
        test ebx, ebx
        nop
        jmp DWORD PTR [zpatch_carparts_resume_if]
    carparts_shadow_skip:
        nop
        jmp DWORD PTR [zpatch_carparts_resume_else]
    }
}

// abi_carparts_shadow_2, recovered VA 0x100068A0: CarDynamicShadowFix's detour. It
// re-runs the shadow call the site overwrote and then repeats it with the
// remaining arguments, calling through the published target rather than a direct
// call, which is what the original's body does.
static __declspec(naked) void CarDynamicShadowShim()
{
    __asm
    {
        push esi
        nop
        push 2
        nop
        push ebx
        nop
        push ebx
        nop
        push edi
        nop
        call DWORD PTR [zpatch_carparts_shadow_fn]
        push esi
        nop
        push 0
        nop
        push ebx
        nop
        push ebx
        nop
        push edi
        nop
        call DWORD PTR [zpatch_carparts_shadow_fn]
        nop
        add esp, 0x14
        nop
        jmp DWORD PTR [zpatch_carparts_resume_2]
    }
}

// abi_car_decals_fix, recovered VA 0x1001B2E0: the fixed twin of the game's decal
// selector (sub_AF7F50), written as a whole function because the fix removes the
// episodic-build test in the middle of it. Everything else is the original's.
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails.
static __declspec(naked) void CarDecalsSelectorShim()
{
    __asm
    {
        mov eax, DWORD PTR [esp + 8]
        mov ecx, DWORD PTR [eax + 0x28]
        and ecx, 0x3C0
        cmp ecx, 0x80
        jnz car_decals_other
        mov eax, DWORD PTR [esp + 4]
        cmp DWORD PTR [eax + 0x38], 0
        jz car_decals_short
        mov eax, DWORD PTR [eax + 0x34]
        ret 8
    car_decals_other:
        mov eax, DWORD PTR [esp + 4]
    car_decals_short:
        mov eax, DWORD PTR [eax + 0x30]
        ret 8
    }
}

// Vehicle fixes. Every arm anchors to its own recovered signature; most writes are
// in-place bytes, jump rewrites and NOP runs, and the four sites whose fix is a
// rewritten instruction sequence jump to a transcribed body instead.
//
// The signatures are forged from the game database (IDA, 1.0.8.0) at each arm's
// scan anchor and are unique in that image; the offsets are measured from the
// anchor, which is why they did not change when the strings did.
namespace
{
    // CarDecalsFix: the decal selector's entry is replaced by its fixed twin, which
    // drops the episodic-build test between the mask compare and the member read.
    void InstallCarDecalsFix()
    {
        if (!Loader::Enabled("CarDecalsFix"))
            return;

        auto pattern = find_pattern("8B 44 24 08 8B 48 28 81 E1 C0 03 00 00 81 F9 80 00 00 00");
        if (pattern.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&CarDecalsSelectorShim), 0x1001B2E0u);
        injector::MakeJMP(pattern.get_first(0), reinterpret_cast<void*>(&CarDecalsSelectorShim), true);
    }

    // CollisionDeadlockFix: the node walk is replaced by its capped twin. The walk
    // test it calls is the site's own call target (the disp32 at match+91), so it
    // is published before the entry is overwritten.
    void InstallCollisionDeadlockFix()
    {
        if (!Loader::Enabled("CollisionDeadlockFix"))
            return;

        auto pattern = find_pattern("55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48 66");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        const auto displacement = *reinterpret_cast<const int32_t*>(match + 91);
        zpatch_collision_should_walk = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 95) + displacement);

        Bodies::Register(reinterpret_cast<uintptr_t>(&CollisionDeadlockShim), 0x10005D80u);
        injector::MakeJMP(match, reinterpret_cast<void*>(&CollisionDeadlockShim), true);
    }

    // CarPartsShadowFix shares the detour with CarDynamicShadowFix: the original
    // gates the rewrite on either key and only the standalone forcing writes below
    // are CarPartsShadowFix's own. The two resumes are the site's own bytes -- the
    // `jle` it would have run (match+2) and the loop exit that `jle` jumps to
    // (match+0x45) -- so they are published before the jump overwrites them.
    void InstallCarPartsShadowFix()
    {
        if (!Loader::Enabled("CarPartsShadowFix") && !Loader::Enabled("CarDynamicShadowFix"))
            return;

        auto pattern = find_pattern("85 DB 7E 41 55 EB 0A");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        zpatch_carparts_resume_if = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 2));
        zpatch_carparts_resume_else = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 0x45));

        Bodies::Register(reinterpret_cast<uintptr_t>(&CarPartsShadowShim), 0x10006880u);
        injector::MakeJMP(match - 6, reinterpret_cast<void*>(&CarPartsShadowShim), true);

        if (!Loader::Enabled("CarPartsShadowFix"))
            return;

        // The standalone arm forces the shadow path on its own: the `jnz` that would
        // skip it becomes a short jump. The original's second standalone site
        // (`83 FA 04 75 0C`) does not exist on 1.0.8.0.
        pattern = find_pattern("83 F8 04 C6 44 24 0E 00 75 0F");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(8), Bytes::ShortJmp, true);
    }

    // CarDynamicShadowFix: the shadow call site is detached so the shadow is drawn
    // twice with the remaining arguments. The called function and the resume are
    // the site's own operands, published before the jump.
    void InstallCarDynamicShadowFix()
    {
        if (!Loader::Enabled("CarDynamicShadowFix"))
            return;

        auto pattern = find_pattern("56 6A 02 53 53 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            const auto resume = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 11));
            const auto displacement = *reinterpret_cast<const int32_t*>(match + 7);
            zpatch_carparts_shadow_fn = resume + displacement;
            zpatch_carparts_resume_2 = resume;

            Bodies::Register(reinterpret_cast<uintptr_t>(&CarDynamicShadowShim), 0x100068A0u);
            injector::MakeJMP(match, reinterpret_cast<void*>(&CarDynamicShadowShim), true);
        }

        // The shadow's record size is forced to 0x100 from 0x104, and the branch that
        // would skip the shadow becomes unconditional.
        pattern = find_pattern("50 68 04 01 00 00 6A 02 6A 00 E8");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(2), Bytes::Zero, true);

        pattern = find_pattern("83 E0 0F 83 F8 03 75 17 F6 86 6C 02 00 00 04");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(6), Bytes::ShortJmp, true);

        // With ForceCarHeadlightShadows off the original also re-encodes the `jz`
        // that skips the shadow as a five-byte `jmp` to the same target, and forces
        // the pushed shadow size from 0x504 to 0x100.
        if (Loader::Enabled("ForceCarHeadlightShadows"))
            return;

        pattern = find_pattern("0F 84 9E 00 00 00 80 7D 28");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            const auto displacement = *reinterpret_cast<const int32_t*>(match + 2);
            injector::MakeJMP(match, match + 6 + displacement, true);
        }

        pattern = find_pattern("51 68 04 05 00 00 6A 02 6A 00 E8");
        if (!pattern.empty())
            injector::WriteMemory<uint32_t>(pattern.get_first(2), 0x100u, true);
    }

    // InfiniteCarBottomDistance: the `ja` of the bottom-distance clamp becomes an
    // unconditional jump over it (`EB 2A`, written as its two bytes -- there is no
    // short-jump API).
    void InstallInfiniteCarBottomDistance()
    {
        if (!Loader::Enabled("InfiniteCarBottomDistance"))
            return;

        auto pattern = find_pattern("0F 2F D0 76 ? 0F 28 C2 EB ? 0F 2F CD");
        if (pattern.empty())
            return;

        auto* site = pattern.get_first<std::byte>(3);
        injector::WriteMemory<std::byte>(site, Bytes::ShortJmp, true);
        injector::WriteMemory<std::byte>(site + 1, Bytes::JumpOverClamp, true);
    }

    // ReversingLightFix: the slot displacement of `mov eax,[edx+64h]` becomes 0x60,
    // so the reversing-light path reads the neighbouring member.
    void InstallReversingLightFix()
    {
        if (!Loader::Enabled("ReversingLightFix"))
            return;

        auto pattern = find_pattern("11 01 00 00 8B 16 8B 42 64 8B CE FF D0 F3 0F 10");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(8), Bytes::Slot60, true);
    }

    // BikeFeetFix: the 12-byte rider-foot block is deleted, so the chopper foot
    // position is left to the game's own (correct) path.
    void InstallBikeFeetFix()
    {
        if (!Loader::Enabled("BikeFeetFix"))
            return;

        auto pattern = find_pattern("39 82 ? ? ? ? 0F 85 ? ? ? ? 39 05");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(12), 12, true);
    }

    // BikePhoneAnimsFix: the six-byte phone-animation branch is deleted.
    void InstallBikePhoneAnimsFix()
    {
        if (!Loader::Enabled("BikePhoneAnimsFix"))
            return;

        auto pattern = find_pattern("80 BE ? ? ? ? ? 0F 85 ? ? ? ? 80 BE ? ? ? ? ? 0F 84 ? ? ? ? 83 3D");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(33), 6, true);
    }

    // PipeBombDropIconSupport: the nine-byte check before the anchor is deleted, so
    // the pipe-bomb drop icon is accepted on the episodic builds.
    void InstallPipeBombDropIconSupport()
    {
        if (!Loader::Enabled("PipeBombDropIconSupport"))
            return;

        auto pattern = find_pattern("83 7D ? ? 75 ? 8B 15");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(-9), 9, true);
    }

    // TeleportProtection: the entry becomes `ret 8`. The original writes exactly three
    // bytes -- C2 08 over the opcode and 00 over the byte after the immediate -- and
    // injector::MakeRET is not equivalent: it stores the opcode through a 4-byte int,
    // which would also zero match+3.
    void InstallTeleportProtection()
    {
        if (!Loader::Enabled("TeleportProtection"))
            return;

        auto pattern = find_pattern("55 8B EC 83 E4 F8 51 53 8B 5D ? 56 57 6A 08");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        injector::WriteMemory<std::byte>(match, Bytes::RetImm16, true);
        injector::WriteMemory<std::byte>(match + 1, Bytes::RetImm16High, true);
        injector::WriteMemory<std::byte>(match + 2, Bytes::Zero, true);
    }

    // IncreaseVehicleStructLimit: the struct count the allocator is asked for becomes
    // 100 instead of the original constant. The signature keeps the absolute address
    // of the structure name, so it is specific to 1.0.8.0.
    void InstallIncreaseVehicleStructLimit()
    {
        if (!Loader::Enabled("IncreaseVehicleStructLimit"))
            return;

        auto pattern = find_pattern("68 34 EE DA 00");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(6), std::byte{ 100 }, true);
    }
}

// Registered like every other arm: the install tier owns the gate.
export void RegisterVehicles()
{
    Loader::InstallEvent() += []
    {
        InstallCarDecalsFix();
        InstallCollisionDeadlockFix();
        InstallCarPartsShadowFix();
        InstallCarDynamicShadowFix();
        InstallInfiniteCarBottomDistance();
        InstallReversingLightFix();
        InstallBikeFeetFix();
        InstallBikePhoneAnimsFix();
        InstallPipeBombDropIconSupport();
        InstallTeleportProtection();
        InstallIncreaseVehicleStructLimit();
    };
}

module;

#include <common.hxx>

export module vehicles;

import bodies;
import bytes;
import common;
import loader;
import patch;

// The car-parts shadow bodies' published operands: C linkage because the
// transcribed asm names them directly. Each slot is the original's own .bss word;
// the two resume_if/else words hold the site's resume addresses (match+2 and
// match+0x45 of the "85 DB 7E 41 55 EB 0A" anchor).
extern "C"
{
    uint32_t zpatch_carparts_resume_if = 0;    // dword_1015BDC8
    uint32_t zpatch_carparts_resume_else = 0;  // dword_1015BDC0
    uint32_t zpatch_carparts_resume_2 = 0;     // dword_1015BDCC
    uint32_t zpatch_carparts_shadow_fn = 0;    // dword_1015B5B8

    // The collision body's node-walk test and the deadlock counter it compares.
    uint32_t zpatch_collision_should_walk = 0;  // dword_1015BDD4
    uint32_t zpatch_collision_iterations = 0;   // dword_1015BDD8

    // The bike-physics detour's published state: the incoming value the clamp
    // rewrites, the caller's EAX saved across the clamp call, and the resume (the
    // site's own `call`, match+9).
    float zpatch_bike_value = 0.0f;    // dword_1015AD7C
    uint32_t zpatch_bike_saved = 0;    // dword_1015AD80
    uint32_t zpatch_bike_resume = 0;   // dword_1015AD84

    // DualVehicleHeadlights' published state, one word per operand its two bodies
    // read: the light-state global the site pushes (the `push 0x1252D90` at
    // match+207), the three call targets both bodies share, the resume, the two
    // light indices the init helper sets, and the eight register slots a body saves
    // before its first draw and restores before its second.
    uint32_t zpatch_dual_bcc = 0;      // dword_10159BCC
    uint32_t zpatch_dual_bd0 = 0;      // dword_10159BD0
    uint32_t zpatch_dual_bd4 = 0;      // dword_10159BD4
    uint32_t zpatch_dual_be0 = 0;      // dword_10159BE0
    uint32_t zpatch_dual_be4 = 0;      // dword_10159BE4
    uint32_t zpatch_dual_light_a = 1;  // dword_1015802C (the original's on-disk 1)
    uint32_t zpatch_dual_light_b = 2;  // dword_10158030 (the original's on-disk 2)
    uint32_t zpatch_dual_r_eax = 0;    // dword_10159BC4
    uint32_t zpatch_dual_r_ebx = 0;    // dword_10159BEC
    uint32_t zpatch_dual_r_ecx = 0;    // dword_10159BD8
    uint32_t zpatch_dual_r_edx = 0;    // dword_10159BF0
    uint32_t zpatch_dual_r_esi = 0;    // dword_10159BC8
    uint32_t zpatch_dual_r_edi = 0;    // dword_10159BE8
    uint32_t zpatch_dual_r_ebp = 0;    // dword_10159BDC
    uint32_t zpatch_dual_r_esp = 0;    // dword_10159BF4
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

// abi_carparts_shadow_2b, recovered VA 0x100068D0: CarDynamicShadowFix's second
// shadow-submission body, the old builds' variant of CarDynamicShadowShim. It makes
// the same double call through the published target and pops the same 20 argument
// bytes, but the first submission pushes 2/0/0 instead of 2/ebx/ebx.
static __declspec(naked) void CarDynamicShadowShimB()
{
    __asm
    {
        push esi
        nop
        push 2
        nop
        push 0
        nop
        push 0
        nop
        push edi
        nop
        call DWORD PTR [zpatch_carparts_shadow_fn]
        nop
        push esi
        nop
        push 0
        nop
        push 0
        nop
        push 0
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

// sub_1001EF30, recovered VA 0x1001EF30: HighFPSBikePhysicsFix's clamp, the helper
// the detour calls between pushad/popad. The original converts the float value to
// double, compares it against the 8-byte constant 0.006666666665 and replaces it
// with 0x3BDA740E when it is below. Written as the same comparison rather than as
// the SSE sequence: the value and the outcome are what the detour's contract is.
static void BikePhysicsClamp() noexcept
{
    // 0x3BDA740E, the constant the original stores when the value is below the
    // threshold. The static assertion pins the float the decimal below is meant to
    // be: the neighbouring decimals are different floats.
    constexpr float kBikeSpeedThreshold = 0.0066666668f;
    static_assert(std::bit_cast<uint32_t>(kBikeSpeedThreshold) == 0x3BDA740Eu);

    const float value = zpatch_bike_value;
    if (0.006666666665 > static_cast<double>(value))
        zpatch_bike_value = kBikeSpeedThreshold;
}

// sub_1001EF60, recovered VA 0x1001EF60: the bike-physics detour. It saves the
// incoming bike value, clamps it through the helper, writes the clamped value back
// into the caller's argument slot, leaves EAX as the caller had it, then re-runs the
// site's first four instructions (`fld [ebp+8]; push ecx; mov ecx,esi; fstp [esp]`)
// and tail-jumps to the site's own `call` (match+9), so the call and the rest of the
// function run unchanged.
//
// `pushad`/`popad` are emitted as bytes because the inline assembler renders those
// mnemonics with a 0x66 operand-size prefix, which the original does not have.
static __declspec(naked) void BikePhysicsDetour()
{
    __asm
    {
        fld DWORD PTR [ebp + 8]
        fstp DWORD PTR [zpatch_bike_value]
        __emit 0x60                       // pushad
        call BikePhysicsClamp
        __emit 0x61                       // popad
        mov DWORD PTR [zpatch_bike_saved], eax
        mov eax, DWORD PTR [zpatch_bike_value]
        mov DWORD PTR [ebp + 8], eax
        mov eax, DWORD PTR [zpatch_bike_saved]
        fld DWORD PTR [ebp + 8]
        push ecx
        mov ecx, esi
        fstp DWORD PTR [esp]
        jmp DWORD PTR [zpatch_bike_resume]
    }
}

// sub_100064D0, recovered VA 0x100064D0: the two light indices the dual-headlight
// bodies push. The original's bodies call it inside pushad/popad, so the clobbering
// is harmless there; the words hold the same values from load time on.
static void DualHeadlightConstants() noexcept
{
    zpatch_dual_light_a = 1;
    zpatch_dual_light_b = 2;
}

// sub_100064F0, recovered VA 0x100064F0: DualVehicleHeadlights' new-build body. It
// saves every GPR (and ESP) into the module words, draws the two headlights through
// the site's own calls -- the game's light-state global, the ped getter, the driver
// test and the light-draw call the site's instructions name -- restoring the caller's
// registers between the draws, then tail-jumps to the site's `pop edi` (match+338).
// The stack offsets are the recovered ones: this variant is the 1.0.6.0-and-later
// light call's frame, and the old-build body below differs only in them.
static __declspec(naked) void DualHeadlightsNewBody()
{
    __asm
    {
        __emit 0x60                          // pushad
        call DualHeadlightConstants
        __emit 0x61                          // popad
        mov DWORD PTR [zpatch_dual_r_eax], eax
        mov DWORD PTR [zpatch_dual_r_ebx], ebx
        mov DWORD PTR [zpatch_dual_r_ecx], ecx
        mov DWORD PTR [zpatch_dual_r_edx], edx
        mov DWORD PTR [zpatch_dual_r_esi], esi
        mov DWORD PTR [zpatch_dual_r_edi], edi
        mov DWORD PTR [zpatch_dual_r_ebp], ebp
        mov DWORD PTR [zpatch_dual_r_esp], esp
        push 0
        call DWORD PTR [zpatch_dual_bd0]
        add esp, 4
        push eax
        mov ecx, edi
        call DWORD PTR [zpatch_dual_be4]
        push eax
        mov eax, DWORD PTR [esp + 30h]
        fld DWORD PTR [esp + 1Ch]
        push 1
        push DWORD PTR [zpatch_dual_light_a]
        mov ecx, DWORD PTR [ebp + 24h]
        sub esp, 10h
        fstp DWORD PTR [esp + 0Ch]
        add eax, 30h
        fld DWORD PTR [esp + 38h]
        fstp DWORD PTR [esp + 8]
        fld DWORD PTR [esp + 30h]
        fstp DWORD PTR [esp + 4]
        fld DWORD PTR [esp + 2Ch]
        fstp DWORD PTR [esp]
        push eax
        push ecx
        movsx edx, BYTE PTR [edi + 40h]
        mov esi, DWORD PTR [edi + 48h]
        mov edi, DWORD PTR [zpatch_dual_bcc]
        lea ecx, [esp + 54h]
        call DWORD PTR [zpatch_dual_bd4]
        add esp, 24h
        mov eax, DWORD PTR [zpatch_dual_r_eax]
        mov ebx, DWORD PTR [zpatch_dual_r_ebx]
        mov ecx, DWORD PTR [zpatch_dual_r_ecx]
        mov edx, DWORD PTR [zpatch_dual_r_edx]
        mov esi, DWORD PTR [zpatch_dual_r_esi]
        mov edi, DWORD PTR [zpatch_dual_r_edi]
        mov ebp, DWORD PTR [zpatch_dual_r_ebp]
        mov esp, DWORD PTR [zpatch_dual_r_esp]
        push 0
        call DWORD PTR [zpatch_dual_bd0]
        add esp, 4
        push eax
        mov ecx, edi
        call DWORD PTR [zpatch_dual_be4]
        fld DWORD PTR [esp + 18h]
        mov ecx, DWORD PTR [esp + 28h]
        push eax
        push 1
        push DWORD PTR [zpatch_dual_light_b]
        mov eax, DWORD PTR [ebp + 24h]
        sub esp, 10h
        fstp DWORD PTR [esp + 0Ch]
        add ecx, 30h
        fld DWORD PTR [esp + 38h]
        fstp DWORD PTR [esp + 8]
        fld DWORD PTR [esp + 30h]
        fstp DWORD PTR [esp + 4]
        fld DWORD PTR [esp + 2Ch]
        fstp DWORD PTR [esp]
        push ecx
        push eax
        movsx edx, BYTE PTR [edi + 40h]
        mov esi, DWORD PTR [edi + 48h]
        mov edi, DWORD PTR [zpatch_dual_bcc]
        lea ecx, [esp + 54h]
        call DWORD PTR [zpatch_dual_bd4]
        add esp, 24h
        jmp DWORD PTR [zpatch_dual_be0]
    }
}

// sub_10006630, recovered VA 0x10006630: DualVehicleHeadlights' old-build body, the
// 1.0.0.0/1.0.4.0/1.0.0.4 variant. Instruction for instruction the same as the new
// body except for the four float offsets each draw reads: this build's light call
// pushes its arguments one dword (then two) further up the frame.
static __declspec(naked) void DualHeadlightsOldBody()
{
    __asm
    {
        __emit 0x60                          // pushad
        call DualHeadlightConstants
        __emit 0x61                          // popad
        mov DWORD PTR [zpatch_dual_r_eax], eax
        mov DWORD PTR [zpatch_dual_r_ebx], ebx
        mov DWORD PTR [zpatch_dual_r_ecx], ecx
        mov DWORD PTR [zpatch_dual_r_edx], edx
        mov DWORD PTR [zpatch_dual_r_esi], esi
        mov DWORD PTR [zpatch_dual_r_edi], edi
        mov DWORD PTR [zpatch_dual_r_ebp], ebp
        mov DWORD PTR [zpatch_dual_r_esp], esp
        push 0
        call DWORD PTR [zpatch_dual_bd0]
        add esp, 4
        push eax
        mov ecx, edi
        call DWORD PTR [zpatch_dual_be4]
        push eax
        mov eax, DWORD PTR [esp + 30h]
        fld DWORD PTR [esp + 20h]
        push 1
        push DWORD PTR [zpatch_dual_light_a]
        mov ecx, DWORD PTR [ebp + 24h]
        sub esp, 10h
        fstp DWORD PTR [esp + 0Ch]
        add eax, 30h
        fld DWORD PTR [esp + 30h]
        fstp DWORD PTR [esp + 8]
        fld DWORD PTR [esp + 2Ch]
        fstp DWORD PTR [esp + 4]
        fld DWORD PTR [esp + 34h]
        fstp DWORD PTR [esp]
        push eax
        push ecx
        movsx edx, BYTE PTR [edi + 40h]
        mov esi, DWORD PTR [edi + 48h]
        mov edi, DWORD PTR [zpatch_dual_bcc]
        lea ecx, [esp + 54h]
        call DWORD PTR [zpatch_dual_bd4]
        add esp, 24h
        mov eax, DWORD PTR [zpatch_dual_r_eax]
        mov ebx, DWORD PTR [zpatch_dual_r_ebx]
        mov ecx, DWORD PTR [zpatch_dual_r_ecx]
        mov edx, DWORD PTR [zpatch_dual_r_edx]
        mov esi, DWORD PTR [zpatch_dual_r_esi]
        mov edi, DWORD PTR [zpatch_dual_r_edi]
        mov ebp, DWORD PTR [zpatch_dual_r_ebp]
        mov esp, DWORD PTR [zpatch_dual_r_esp]
        push 0
        call DWORD PTR [zpatch_dual_bd0]
        add esp, 4
        push eax
        mov ecx, edi
        call DWORD PTR [zpatch_dual_be4]
        fld DWORD PTR [esp + 1Ch]
        mov ecx, DWORD PTR [esp + 28h]
        push eax
        push 1
        push DWORD PTR [zpatch_dual_light_b]
        mov eax, DWORD PTR [ebp + 24h]
        sub esp, 10h
        fstp DWORD PTR [esp + 0Ch]
        add ecx, 30h
        fld DWORD PTR [esp + 30h]
        fstp DWORD PTR [esp + 8]
        fld DWORD PTR [esp + 2Ch]
        fstp DWORD PTR [esp + 4]
        fld DWORD PTR [esp + 34h]
        fstp DWORD PTR [esp]
        push ecx
        push eax
        movsx edx, BYTE PTR [edi + 40h]
        mov esi, DWORD PTR [edi + 48h]
        mov edi, DWORD PTR [zpatch_dual_bcc]
        lea ecx, [esp + 54h]
        call DWORD PTR [zpatch_dual_bd4]
        add esp, 24h
        jmp DWORD PTR [zpatch_dual_be0]
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
    // One recovered sub-gate: is the current build's id a bit of `mask`? The
    // catalogue's own mask is the arm's gate; a few recovered rows carry a narrower
    // one of their own, which is what this reads.
    bool CurrentBuildInMask(uint32_t mask)
    {
        return (mask & (1u << static_cast<uint32_t>(Loader::Current()))) != 0;
    }

    // CarDynamicShadowFix@noE's own gate: the recovered table gives the row the
    // mask 0x22 (build ids 1000 and 1040), the builds where the installer's pattern
    // E is absent. 1.0.0.4 has pattern E, so it keeps the E path and is not in the
    // mask even though pattern C matches there too.
    constexpr uint32_t kCarDynamicShadowNoEBuilds = 0x22u;

    // DualVehicleHeadlights' two recovered arm gates: the original's own table gives
    // the old body 0x62 (build ids 1, 5 and 6 -- 1.0.0.0, 1.0.4.0, 1.0.0.4) and the
    // new body 0x4BF80 (id 7 and every later build). The original's handler selects
    // between the two by the build id it classified (`cmp [build], 1` .. `cmp
    // [build], 6`, 0x100067F3), which is the same split. The union is every
    // supported build.
    constexpr uint32_t kDualHeadlightsOldBuilds = 0x62u;
    constexpr uint32_t kDualHeadlightsNewBuilds = 0x4BF80u;

    // A captured pattern operand is an address in the image's own coordinate
    // system. The dump places that image at a heap address, so the operand is
    // rebased through the image's preferred base; in the game the two are equal.
    // Only the arms that write *through* such an operand need it.
    template <typename T>
    T* GamePointer(uint32_t value)
    {
        const auto host = Game::Get().ImageBase();
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(host);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(host + dos->e_lfanew);
        return reinterpret_cast<T*>(host + (value - static_cast<uint32_t>(nt->OptionalHeader.ImageBase)));
    }

    // A rel32 target inside the match: the displacement dword at `displacement`,
    // measured from the instruction's last byte at `next`.
    uint32_t Rel32Target(std::byte* match, std::ptrdiff_t displacement, std::ptrdiff_t next)
    {
        const auto displacementValue = *reinterpret_cast<const int32_t*>(match + displacement);
        return static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(match + next) + static_cast<uint32_t>(displacementValue));
    }

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

    // CollisionDeadlockFix's published walk test: the site's own call target (the
    // disp32 at match+91), read before the install overwrites the entry.
    void PublishCollisionDeadlockFix()
    {
        if (!Loader::Enabled("CollisionDeadlockFix"))
            return;

        auto pattern = find_pattern("55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48 66");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        const auto displacement = *reinterpret_cast<const int32_t*>(match + 91);
        zpatch_collision_should_walk =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 95) + displacement);
    }

    // CollisionDeadlockFix: the node walk is replaced by its capped twin.
    void InstallCollisionDeadlockFix()
    {
        if (!Loader::Enabled("CollisionDeadlockFix"))
            return;

        auto pattern = find_pattern("55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48 66");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        Bodies::Register(reinterpret_cast<uintptr_t>(&CollisionDeadlockShim), 0x10005D80u);
        injector::MakeJMP(match, reinterpret_cast<void*>(&CollisionDeadlockShim), true);
    }

    // HighFPSBikePhysicsFix's resume: the site's own `call` at match+9, read before
    // the install replaces the first five bytes with the jump. The body re-runs those
    // five bytes' instructions and tail-jumps to the call, so the call and the rest of
    // the function run unchanged.
    void PublishHighFPSBikePhysicsFix()
    {
        if (!Loader::Enabled("HighFPSBikePhysicsFix"))
            return;

        auto pattern = find_pattern("D9 45 08 51 8B CE D9 1C 24 E8 ?? ?? ?? ?? 8A 86 90 14 00 00");
        if (pattern.empty())
            return;

        zpatch_bike_resume =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(9)));
    }

    // HighFPSBikePhysicsFix: the bike's per-frame speed value passes through the
    // clamp before the physics step consumes it.
    void InstallHighFPSBikePhysicsFix()
    {
        if (!Loader::Enabled("HighFPSBikePhysicsFix"))
            return;

        auto pattern = find_pattern("D9 45 08 51 8B CE D9 1C 24 E8 ?? ?? ?? ?? 8A 86 90 14 00 00");
        if (pattern.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&BikePhysicsDetour), 0x1001EF60u);
        injector::MakeJMP(pattern.get_first<std::byte>(0), reinterpret_cast<void*>(&BikePhysicsDetour), true);
    }

    // DualVehicleHeadlights' published operands, all read from the one match: the
    // light-state global the site pushes at match+207, the ped getter's call target
    // (the disp32 at match+140, next byte +144), the driver test's (+151/+155), the
    // draw call's (+331/+335) and the resume (`pop edi`, match+338). The site's own
    // instructions carry those operands, so the publish is a read, not a decode.
    void PublishDualVehicleHeadlights()
    {
        // The catalogue's key mask cannot be the gate here: see the install arm.
        if (Catalogue::Read("DualVehicleHeadlights") != 1)
            return;

        auto pattern = find_pattern("8B 17 8B 42 64 8B CF FF D0 8B 54");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        zpatch_dual_bcc = *pattern.get_first<uint32_t>(207);
        zpatch_dual_bd0 = Rel32Target(match, 140, 144);
        zpatch_dual_be4 = Rel32Target(match, 151, 155);
        zpatch_dual_bd4 = Rel32Target(match, 331, 335);
        zpatch_dual_be0 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 338));
    }

    // DualVehicleHeadlights: both recovered arms jump to their own body at the same
    // site, and the original's handler picks the body from the build id it classified
    // (`cmp [build], 1` .. `cmp [build], 6` at 0x100067F3): ids 1..6 get the old body,
    // every later build the new one. That is the pair of recovered masks, so each
    // build runs exactly one arm; the reference tree ran both unconditionally and the
    // later write won, which lands on the same bytes everywhere the masks are
    // disjoint (they are).
    //
    // The gate is the catalogue's INI read alone. This key's catalogue mask (0x20) is
    // the feature document's build list, not the original's: the original's own two
    // arm masks cover every supported build, so `Loader::Enabled` would never install
    // this arm anywhere but 1.0.4.0. See docs/parity.md.
    void InstallDualVehicleHeadlights()
    {
        if (Catalogue::Read("DualVehicleHeadlights") != 1)
            return;

        auto pattern = find_pattern("8B 17 8B 42 64 8B CF FF D0 8B 54");
        if (pattern.empty())
            return;

        if (CurrentBuildInMask(kDualHeadlightsOldBuilds))
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&DualHeadlightsOldBody), 0x10006630u);
            injector::MakeJMP(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&DualHeadlightsOldBody), true);
        }

        if (CurrentBuildInMask(kDualHeadlightsNewBuilds))
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&DualHeadlightsNewBody), 0x100064F0u);
            injector::MakeJMP(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&DualHeadlightsNewBody), true);
        }
    }

    // CarPartsShadowFix's published resumes: the site's own bytes -- the `jle` it
    // would have run (match+2) and the loop exit that `jle` jumps to (match+0x45).
    // The shared detour is installed under either key, so the publish uses the same
    // combined gate.
    void PublishCarPartsShadowFix()
    {
        if (!Loader::Enabled("CarPartsShadowFix") && !Loader::Enabled("CarDynamicShadowFix"))
            return;

        auto pattern = find_pattern("85 DB 7E 41 55 EB 0A");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        zpatch_carparts_resume_if = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 2));
        zpatch_carparts_resume_else =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 0x45));
    }

    // CarPartsShadowFix shares the detour with CarDynamicShadowFix: the original
    // gates the rewrite on either key and only the standalone forcing writes below
    // are CarPartsShadowFix's own.
    void InstallCarPartsShadowFix()
    {
        if (!Loader::Enabled("CarPartsShadowFix") && !Loader::Enabled("CarDynamicShadowFix"))
            return;

        auto pattern = find_pattern("85 DB 7E 41 55 EB 0A");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);

        Bodies::Register(reinterpret_cast<uintptr_t>(&CarPartsShadowShim), 0x10006880u);
        injector::MakeJMP(match - 6, reinterpret_cast<void*>(&CarPartsShadowShim), true);

        if (!Loader::Enabled("CarPartsShadowFix"))
            return;

        // The standalone arm forces the shadow path on its own: the `jnz` that would
        // skip it becomes a short jump. The recovered standalone handler scans two
        // variants: the `83 F8 04` site (1.0.6.0 and later) and the `83 FA 04 75 0C`
        // site (the older builds). Each scan is a clean skip on the builds that do
        // not carry it.
        pattern = find_pattern("83 F8 04 C6 44 24 0E 00 75 0F");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(8), Bytes::ShortJmp, true);

        pattern = find_pattern("83 FA 04 75 0C");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(3), Bytes::ShortJmp, true);
    }

    // CarDynamicShadowFix's published call target and resume, from whichever body
    // variant the build carries: pattern A's `call` sits at match+6, so its resume is
    // match+0xB and the displacement is at match+7; pattern B's `call` sits at
    // match+8, so its resume is match+0xD and the displacement is at match+9.
    void PublishCarDynamicShadowFix()
    {
        if (!Loader::Enabled("CarDynamicShadowFix"))
            return;

        auto pattern = find_pattern("56 6A 02 53 53 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            const auto resume = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 11));
            zpatch_carparts_shadow_fn = resume + *reinterpret_cast<const int32_t*>(match + 7);
            zpatch_carparts_resume_2 = resume;
            return;
        }

        pattern = find_pattern("56 6A 02 6A 00 6A 00 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            const auto resume = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 13));
            zpatch_carparts_shadow_fn = resume + *reinterpret_cast<const int32_t*>(match + 9);
            zpatch_carparts_resume_2 = resume;
        }
    }

    // CarDynamicShadowFix: the shadow call site is detached so the shadow is drawn
    // twice with the remaining arguments. Two recovered body variants exist: pattern
    // A (1.0.6.0 and later) and pattern B (the older builds); the key's own build
    // mask covers both, and the signatures are the gate. The same installer's C and
    // E branches are the old builds' two extra writes: a range check rewritten as an
    // unconditional jump, and the six-byte NOP fill where pattern E is absent.
    void InstallCarDynamicShadowFix()
    {
        if (!Loader::Enabled("CarDynamicShadowFix"))
            return;

        auto pattern = find_pattern("56 6A 02 53 53 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            Bodies::Register(reinterpret_cast<uintptr_t>(&CarDynamicShadowShim), 0x100068A0u);
            injector::MakeJMP(match, reinterpret_cast<void*>(&CarDynamicShadowShim), true);
        }

        // Pattern B is the old-build variant of the same call site.
        pattern = find_pattern("56 6A 02 6A 00 6A 00 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            Bodies::Register(reinterpret_cast<uintptr_t>(&CarDynamicShadowShimB), 0x100068D0u);
            injector::MakeJMP(match, reinterpret_cast<void*>(&CarDynamicShadowShimB), true);
        }

        // Pattern C: the double range check (`>= 0x14` / `>= 6`) is skipped by
        // turning the second test's `jnz` into a jump over it. The row exists only on
        // the older builds, which is also where the @noE fill below belongs.
        pattern = find_pattern("83 F8 14 7D 05 83 F8 06 7D 14");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            injector::MakeJMP(match + 3, match + 0x1E, true);

            // CarDynamicShadowFix@noE: with pattern E absent (the recovered gate is
            // build ids 1000 and 1040) the installer NOP-fills six bytes after the C
            // anchor. 1.0.0.4 has pattern E, so it is not in the mask.
            if (CurrentBuildInMask(kCarDynamicShadowNoEBuilds))
                injector::MakeNOP(match + 62, 6, true);
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

    // EpisodicVehicleSupport's own episode word: the game's unk_10158128, an int
    // holding 2, which every `cmp [episode], 2` below tests. The arm hands the game
    // this module's word instead, so the four 1.0.7.0/1.0.8.0 sites read a value
    // this module owns; the dump canonicalises the four published operands.
    uint32_t zpatch_episode_flag = 2;

    // EpisodicVehicleSupport: the episode test at each site is deleted, so every
    // vehicle check that is gated on "not episodic" runs on the episodic builds.
    //
    // Two parts, because the original's handler has two:
    //   * 1.0.7.0 and 1.0.8.0 each retarget the absolute operand of two
    //     `cmp dword ptr [episode], 2` sites (the first also a `jnz`, the second a
    //     `jne rel32`) at this module's episode word. The recovered signatures keep
    //     the build's own operand bytes, so each build has its own pair -- and the
    //     operand itself is the match, which is why the write lands at +0;
    //   * every gated build then gets the 23 signature-driven NOP rows, in the
    //     original's row order. The first two rows write every hit of a multi-hit
    //     scan (two each on this image) and two rows carry a second window.
    void InstallEpisodicVehicleSupport()
    {
        if (!Loader::Enabled("EpisodicVehicleSupport"))
            return;

        const auto flag = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_episode_flag));
        if (Loader::Current() == Build::v1070)
        {
            auto pattern = find_pattern("44 80 0F ?? 02 75 ?? E8 ?? ?? ?? ?? 3B C3 74 ?? 8D");
            if (!pattern.empty())
            {
                auto* site = pattern.get_first<std::byte>(0);
                injector::WriteMemory<uint32_t>(site, flag, true);
                Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(site));
            }

            pattern = find_pattern("44 80 0F ?? 02 0F 85 94 00 ?? ?? ?? ?? 24 ?? 85 D2 0F 84 ?? ?? ?? ?? 8B");
            if (!pattern.empty())
            {
                auto* site = pattern.get_first<std::byte>(0);
                injector::WriteMemory<uint32_t>(site, flag, true);
                Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(site));
            }
        }

        if (Loader::Current() == Build::v1080)
        {
            auto pattern = find_pattern("54 D7 12 01 02 75 ?? E8 ?? ?? ?? ?? 3B C3 74 ?? 8D");
            if (!pattern.empty())
            {
                auto* site = pattern.get_first<std::byte>(0);
                injector::WriteMemory<uint32_t>(site, flag, true);
                Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(site));
            }

            pattern = find_pattern("54 D7 12 01 02 0F 85 94 00 ?? ?? ?? ?? 24 ?? 85 D2 0F 84 ?? ?? ?? ?? 8B");
            if (!pattern.empty())
            {
                auto* site = pattern.get_first<std::byte>(0);
                injector::WriteMemory<uint32_t>(site, flag, true);
                Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(site));
            }
        }

        // Row 1: the `jl` of a loop that only runs on the episodic builds. Two
        // sites, both rewritten (the original writes hits[0] and hits[1]).
        auto pattern = find_pattern<2>("7C 18 0F BF 4E 2E");
        for (size_t hit = 0; hit < pattern.size(); ++hit)
            injector::MakeNOP(pattern.get(hit).get<std::byte>(-6), 8, true);

        // Row 2: the same `cmp [episode], 2` shape twice in two functions.
        pattern = find_pattern<2>("83 3D ?? ?? ?? ?? 02 75 17 F3 0F 10 44 24 08");
        for (size_t hit = 0; hit < pattern.size(); ++hit)
            injector::MakeNOP(pattern.get(hit).get<std::byte>(0), 9, true);

        // Rows 3-20: one window each.
        pattern = find_pattern("39 35 ?? ?? ?? ?? 75 0E 80 3D");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(6), 2, true);

        pattern = find_pattern("74 17 83 3D ?? ?? ?? ?? 02 75 0E F3 0F 10 05");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(2), 9, true);

        pattern = find_pattern("7C 22 83 F8 28 75 1D");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 2, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 0F 8C 0E 05 00 00");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 13, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 75 08 3B 05");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 9, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 75 17 F3 0F 10 44 24 0C");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 9, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 75 1F 8B 44 24 04");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 9, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 75 21 8B 96 20 08 00 00");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 9, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 75 24 F6 87 6C 02 00 00 04");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 9, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 75 35 0F BF 46 2E");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 9, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 75 38 8B 16");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 9, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 7C 11 8B 4E");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(7), 2, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 7C 1F 8B");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(7), 2, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 7C 26 8B 85 40 0B 00 00");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 9, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 7C 35 0F BF 43 2E");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 9, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 7C 43");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(7), 2, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 7C 4A 0F BF 46 2E");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 13, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 89 44 24 38");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(11), 6, true);

        // Row 21: the branch and the byte it would skip to (a second window at +17).
        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 8A 0D");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            injector::MakeNOP(match, 7, true);
            injector::MakeNOP(match + 17, 2, true);
        }

        // Row 22: the float load's branch.
        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 F3 0F 10 05 ?? ?? ?? ?? F3 0F 11 44 24 20 75 4C");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(21), 2, true);

        // Row 23: the loop head and its `jl` (a second window at +10).
        pattern = find_pattern("8B 56 04 8B 4A 0C 89 4C 24 1C 7C 18");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            injector::MakeNOP(match - 15, 7, true);
            injector::MakeNOP(match + 10, 2, true);
        }
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

    // IncreaseVehicleModelLimit: the vehicle-model cap the loop bound is compared
    // against becomes 1024. The site is the `mov ecx, 0xF3E8E8` at match+77: the
    // arm reads that operand, which is the address of the model-limit object the
    // allocator is constructed with, and writes the new cap through it. The write
    // is a game-data dword, not a published pointer, so there is no slot to
    // register with the dump.
    void InstallIncreaseVehicleModelLimit()
    {
        if (!Loader::Enabled("IncreaseVehicleModelLimit"))
            return;

        auto pattern = find_pattern("83 C7 01 81 FF 18 79 00 00 7C D2");
        if (pattern.empty())
            return;

        constexpr uint32_t kVehicleModelLimit = 0x400u;
        injector::WriteMemory<uint32_t>(
            GamePointer<uint32_t>(*pattern.get_first<uint32_t>(77)), kVehicleModelLimit, true);
    }
}

// Registered like every other arm: the publish tier reads every site the install
// tier is about to overwrite, and the install tier owns the gate and the writes.
export void RegisterVehicles()
{
    Loader::PublishEvent() += []
    {
        PublishCollisionDeadlockFix();
        PublishCarPartsShadowFix();
        PublishCarDynamicShadowFix();
        PublishHighFPSBikePhysicsFix();
        PublishDualVehicleHeadlights();
    };
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
        InstallEpisodicVehicleSupport();
        InstallHighFPSBikePhysicsFix();
        InstallPipeBombDropIconSupport();
        InstallTeleportProtection();
        InstallDualVehicleHeadlights();
        InstallIncreaseVehicleModelLimit();
        InstallIncreaseVehicleStructLimit();
    };
}

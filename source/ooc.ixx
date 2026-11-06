module;

#include <common.hxx>

// Declares the /GS cookie the transcribed wrapper's prologue reads and the check
// its epilogue calls (`__security_cookie`, `__security_check_cookie` in
// vcruntime.h), the `va_list` the rebuilt formatter takes, and the CRT calls the
// decoder and the archive builder make.
#include <intrin.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

export module ooc;

import bodies;
import common;
import loader;
import patch;

// OutOfCommissionFix, the one key whose payload is data rather than code: the
// original embeds the compressed `finale2.sco` script resource in its own image,
// decodes it at run time, registers the decoded table as a fake pak entry, wraps
// it in an in-memory archive and mounts that archive through the game's own
// device register. The arm is then two writes: an `E9` that widens the game's
// script-resource magic check to accept the archive's `0x86723DFF`, and an `E8`
// that sends the game's device lookup through the mounting wrapper first.
//
// Recovered VAs (IDA `ZolikaPatch.asi.i64`):
//
//   0x1004C3F0  the key's handler: scan `A9 39 44 24 18`, publish match+0x11 as
//               the branch body's resume and `E9` the match's +7; scan
//               `E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56`, publish the target
//               of the call at match-0x33 (the game's device register) and the
//               original target of the call at the match, then retarget that call
//               to the wrapper. Compiler output (EH4 frame, /GS, std::string
//               temporaries, the string builder's digit loops), so it is rebuilt
//               below rather than transcribed and has no proof row.
//   0x1004B150  the decoder's opcode step.                transcribed, proved
//   0x1004B570  the decode driver + Adler-32.             transcribed, proved
//   0x1004B750  the init: decode, register, build the archive, publish the size.
//   0x1004ACE0  the in-memory archive builder.  rebuilt, no proof row
//   0x1004C350  the wrapper the second `E8` is retargeted to.  transcribed, proved
//   0x1004C3C0  the branch body the `E9` jumps to.             transcribed, proved
//
// The decoder pair is transcribed instruction for instruction from the original's
// own stream: an interpreter with sixteen exits and no register contract beyond
// "ECX in, EAX out", no exception frame, no TLS guard and no indexed absolute
// operand, so it is provable whole (`tests/bodies.tsv`). The remaining rebuilds are
// compiler output for a plain C function: the archive builder is a memset plus a
// sequence of memcpys, the init is C++ container plumbing and the handler is the
// original's own loader-shaped scan/publish/write code. They are rebuilt in C++
// with the original's arithmetic, bounds and call order, so their behaviour is the
// same and there is nothing instruction-level to compare.

// The recovered resource, verbatim. source/ooc_finale2.inc carries the pinned
// image SHA-256, the source VA and length, the extraction recipe and the decoded
// table's own hashes.
static const uint8_t kOocResource[] = {
#include "ooc_finale2.inc"
};
static_assert(sizeof(kOocResource) == 163796,
    "the recovered resource is the original's 163,796 bytes at 0x100B6798");

// The original's own state, in C linkage because the transcribed bodies name the
// slots directly (the original keeps them in its .bss beside its bodies).
extern "C"
{
    uint32_t zpatch_ooc_resume = 0;    // dword_10158034: the branch body's resume
    uint32_t zpatch_ooc_mount = 0;     // dword_10159BF8: the game's device register
    uint32_t zpatch_ooc_original = 0;  // dword_10159C04: the retargeted call's target
    uint32_t zpatch_ooc_state = 0;     // dword_10159BFC: the archive's byte count

    // The decode stream's four words, which the original keeps in its own .bss and
    // its transcribed bodies read as absolute operands: the output buffer's start
    // (dword_101A842C), its cursor (dword_101A8430), its end (dword_10196374) and
    // the source's lower bound (dword_101887D4 = the resource's base - 16).
    uint32_t zpatch_ooc_begin = 0;
    uint32_t zpatch_ooc_cursor = 0;
    uint32_t zpatch_ooc_end = 0;
    uint32_t zpatch_ooc_lower = 0;
}

// The two halves of the mount string the wrapper formats: "me" + "mor" + "y" +
// ':' + '$' + <archive> + ',' + <bytes> + ',' + '0' + ':' + "" is
// "memory:$<archive>,<bytes>,0:", which is the name the game's device register
// takes for a block that already sits in memory. The format and its one string
// argument are the original's own text (0x1012EB58 and 0x1012EB0C).
extern "C"
{
    const char zpatch_ooc_mount_format[] = "me%sy%c%c%p,%d,%c%c%s";
    const char zpatch_ooc_mount_prefix[] = "mor";
}

// The bounded formatter's entry, published by source/stability.ixx (sub_10001280).
extern "C"
{
    extern uint32_t zpatch_format_entry;
}

namespace
{
    // sub_1004B750's malloc argument and sub_1004B570's output bound
    // (dword_10196374 = begin + 0x2BD50): the decoded table's exact size.
    constexpr size_t kOocTableSize = 0x2BD50;  // 179,536
    // The archive's granularity: the payload starts at 0x800 and every entry is
    // padded to it (sub_1004ACE0, sub_1004ABB0).
    constexpr size_t kOocPageSize = 0x800;
    // The name the one fake-pak entry carries, and the archive's own magic -- the
    // value the branch body compares the script resource against.
    constexpr char kOocName[] = "finale2.sco";
    constexpr uint32_t kOocArchiveMagic = 0x86723DFFu;
}

// The decode pair's one call, the CRT's memcpy. The inline assembler refuses
// `call memcpy` (it is a compiler intrinsic), so the three copy sites call this
// thin wrapper instead; the original calls the CRT directly, the call shape is the
// same (`call imm`), the caller cleans the three arguments and the copy itself is
// the CRT's.
static void __cdecl OocCopyBlock(void* destination, const void* source, std::size_t size)
{
    std::memcpy(destination, source, size);
}

// sub_1004B150, recovered VA 0x1004B150 (406 instructions): the decoder's opcode
// step. ECX is the stream's source cursor on entry and EAX the next one on exit --
// the original's own contract, and a step that does not advance answers its input,
// which is the driver's stop signal. The stream's four words are absolute operands
// in the original (its own .bss), so they are module slots here; the three `call`
// sites are the CRT's memcpy through the wrapper above.
//
// The dispatch is the original's chain of comparisons, not a switch: `>= 0x80` is
// the two-byte back-reference, `>= 0x40` the three-byte one biased by 0x3FFF,
// `>= 0x20` the short literal whose count is the opcode less 31, and below that
// 0x18/0x10/0x08/7/6/4 in that order. Everything else (0, 1, 2, 3, 5) falls through
// to the shared epilogue with the cursor unchanged.
static __declspec(naked) void OocStepBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        push ecx
        push ebx
        push esi
        mov esi, ecx
        push edi
        mov dl, byte ptr [esi]
        cmp dl, 20h
        jb ooc_step_below20
        movzx ebx, dl
        lea eax, [esi + 1]
        cmp dl, 80h
        jb ooc_step_40
        mov ecx, dword ptr [zpatch_ooc_cursor]
        lea edi, [ebx - 7Fh]
        movzx eax, byte ptr [eax]
        mov edx, ecx
        mov ebx, dword ptr [zpatch_ooc_end]
        sub edx, eax
        dec edx
        lea eax, [ecx + edi]
        cmp eax, ebx
        ja ooc_step_ov2
        cmp edx, dword ptr [zpatch_ooc_begin]
        jae ooc_step_copy2
        lea eax, [ebx + 1]
    ooc_step_ov2:
        add esi, 2
        mov dword ptr [zpatch_ooc_cursor], eax
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_copy2:
        test edi, edi
        je ooc_step_ret2
        // The original's three-byte alignment NOP (0F 1F 00, `nop dword ptr [eax]`).
        // The inline assembler refuses `nop` with a memory operand and has no `db`,
        // so the bytes are emitted one at a time; they are load-bearing, because
        // every instruction after them sits at the same offset on both sides and
        // the proof compares internal branch targets by offset.
        _emit 0x0F
        _emit 0x1F
        _emit 0x00
    ooc_step_copy2_loop:
        mov al, byte ptr [edx]
        lea edx, [edx + 1]
        mov byte ptr [ecx], al
        inc ecx
        sub edi, 1
        jne ooc_step_copy2_loop
        mov dword ptr [zpatch_ooc_cursor], ecx
    ooc_step_ret2:
        add esi, 2
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_40:
        cmp dl, 40h
        jb ooc_step_20
        movzx edi, byte ptr [esi + 2]
        mov ecx, dword ptr [zpatch_ooc_cursor]
        inc edi
        movzx eax, byte ptr [eax]
        mov edx, ecx
        shl ebx, 8
        sub edx, ebx
        mov ebx, dword ptr [zpatch_ooc_end]
        sub edx, eax
        lea eax, [ecx + edi]
        add edx, 3FFFh
        cmp eax, ebx
        ja ooc_step_ov3
        cmp edx, dword ptr [zpatch_ooc_begin]
        jae ooc_step_copy3
        lea eax, [ebx + 1]
    ooc_step_ov3:
        add esi, 3
        mov dword ptr [zpatch_ooc_cursor], eax
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
        // The original's five-byte alignment NOP (66 0F 1F 44 00 00, `nop word ptr
        // [eax+eax]` with a zero displacement). The inline assembler only forms the
        // four-byte encoding of that instruction, and the extra byte is
        // load-bearing: every instruction after it sits one byte later and the
        // proof compares internal branch targets by offset.
        _emit 0x66
        _emit 0x0F
        _emit 0x1F
        _emit 0x44
        _emit 0x00
        _emit 0x00
    ooc_step_copy3:
        mov al, byte ptr [edx]
        lea edx, [edx + 1]
        mov byte ptr [ecx], al
        inc ecx
        sub edi, 1
        jne ooc_step_copy3
        add esi, 3
        mov dword ptr [zpatch_ooc_cursor], ecx
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_20:
        mov edi, dword ptr [zpatch_ooc_cursor]
        add ebx, -31
        mov ecx, dword ptr [zpatch_ooc_end]
        add edi, ebx
        cmp edi, ecx
        ja ooc_step_lit_over
        cmp eax, dword ptr [zpatch_ooc_lower]
        jae ooc_step_lit_copy
        lea edi, [ecx + 1]
        movzx eax, dl
        add esi, -30
        mov dword ptr [zpatch_ooc_cursor], edi
        add esi, eax
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_lit_copy:
        push ebx
        push eax
        push dword ptr [zpatch_ooc_cursor]
        call OocCopyBlock
        mov dl, byte ptr [esi]
        add esp, 0Ch
    ooc_step_lit_over:
        movzx eax, dl
        add esi, -30
        add esi, eax
        mov dword ptr [zpatch_ooc_cursor], edi
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_below20:
        cmp dl, 18h
        jb ooc_step_below18
        movzx eax, byte ptr [esi + 1]
        mov ecx, dword ptr [zpatch_ooc_cursor]
        mov edi, ecx
        movzx ebx, byte ptr [esi + 3]
        shl eax, 8
        inc ebx
        sub edi, eax
        movzx eax, dl
        mov edx, dword ptr [zpatch_ooc_end]
        shl eax, 10h
        sub edi, eax
        movzx eax, byte ptr [esi + 2]
        sub edi, eax
        lea eax, [ecx + ebx]
        add edi, 17FFFFh
        cmp eax, edx
        ja ooc_step_ov4
        cmp edi, dword ptr [zpatch_ooc_begin]
        jae ooc_step_copy4
        lea eax, [edx + 1]
    ooc_step_ov4:
        add esi, 4
        mov dword ptr [zpatch_ooc_cursor], eax
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_copy4:
        mov al, byte ptr [edi]
        lea edi, [edi + 1]
        mov byte ptr [ecx], al
        inc ecx
        sub ebx, 1
        jne ooc_step_copy4
        add esi, 4
        mov dword ptr [zpatch_ooc_cursor], ecx
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_below18:
        cmp dl, 10h
        jb ooc_step_below10
        movzx ecx, byte ptr [esi + 3]
        movzx ebx, byte ptr [esi + 4]
        movzx eax, byte ptr [esi + 1]
        inc ebx
        shl ecx, 8
        shl eax, 8
        add ebx, ecx
        mov ecx, dword ptr [zpatch_ooc_cursor]
        mov edi, ecx
        sub edi, eax
        movzx eax, dl
        mov edx, dword ptr [zpatch_ooc_end]
        shl eax, 10h
        sub edi, eax
        movzx eax, byte ptr [esi + 2]
        sub edi, eax
        lea eax, [ecx + ebx]
        add edi, 0FFFFFh
        cmp eax, edx
        ja ooc_step_ov5
        cmp edi, dword ptr [zpatch_ooc_begin]
        jae ooc_step_copy5
        lea eax, [edx + 1]
    ooc_step_ov5:
        add esi, 5
        mov dword ptr [zpatch_ooc_cursor], eax
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_copy5:
        mov al, byte ptr [edi]
        lea edi, [edi + 1]
        mov byte ptr [ecx], al
        inc ecx
        sub ebx, 1
        jne ooc_step_copy5
        add esi, 5
        mov dword ptr [zpatch_ooc_cursor], ecx
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_below10:
        cmp dl, 8
        jb ooc_step_below8
        mov bl, byte ptr [esi + 1]
        mov edi, dword ptr [zpatch_ooc_cursor]
        movzx ecx, dl
        shl ecx, 8
        movzx eax, bl
        add eax, 0FFFFF801h
        add ecx, eax
        lea eax, [esi + 2]
        add edi, ecx
        mov dword ptr [ebp - 4], ecx
        mov ecx, dword ptr [zpatch_ooc_end]
        cmp edi, ecx
        ja ooc_step_ov6
        cmp eax, dword ptr [zpatch_ooc_lower]
        jae ooc_step_copy6
        lea edi, [ecx + 1]
        jmp ooc_step_ov6
    ooc_step_copy6:
        push dword ptr [ebp - 4]
        push eax
        push dword ptr [zpatch_ooc_cursor]
        call OocCopyBlock
        mov dl, byte ptr [esi]
        add esp, 0Ch
        mov bl, byte ptr [esi + 1]
    ooc_step_ov6:
        movzx ecx, dl
        shl ecx, 8
        movzx eax, bl
        add eax, esi
        mov dword ptr [zpatch_ooc_cursor], edi
        pop edi
        lea esi, [ecx - 7FDh]
        add esi, eax
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_below8:
        cmp dl, 7
        jne ooc_step_below7
        mov dl, byte ptr [esi + 1]
        mov bl, byte ptr [esi + 2]
        mov edi, dword ptr [zpatch_ooc_cursor]
        movzx ecx, dl
        shl ecx, 8
        movzx eax, bl
        inc eax
        add ecx, eax
        lea eax, [esi + 3]
        add edi, ecx
        mov dword ptr [ebp - 4], ecx
        mov ecx, dword ptr [zpatch_ooc_end]
        cmp edi, ecx
        ja ooc_step_ov7
        cmp eax, dword ptr [zpatch_ooc_lower]
        jae ooc_step_copy7
        lea edi, [ecx + 1]
        jmp ooc_step_ov7
    ooc_step_copy7:
        push dword ptr [ebp - 4]
        push eax
        push dword ptr [zpatch_ooc_cursor]
        call OocCopyBlock
        mov dl, byte ptr [esi + 1]
        add esp, 0Ch
        mov bl, byte ptr [esi + 2]
    ooc_step_ov7:
        movzx ecx, dl
        shl ecx, 8
        movzx eax, bl
        add eax, esi
        mov dword ptr [zpatch_ooc_cursor], edi
        pop edi
        lea esi, [ecx + 4]
        add esi, eax
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_below7:
        cmp dl, 6
        jne ooc_step_below6
        movzx eax, byte ptr [esi + 1]
        mov edx, dword ptr [zpatch_ooc_cursor]
        mov ecx, edx
        movzx edi, byte ptr [esi + 4]
        mov ebx, dword ptr [zpatch_ooc_end]
        inc edi
        shl eax, 10h
        sub ecx, eax
        movzx eax, byte ptr [esi + 2]
        shl eax, 8
        sub ecx, eax
        movzx eax, byte ptr [esi + 3]
        sub ecx, eax
        lea eax, [edx + edi]
        dec ecx
        cmp eax, ebx
        ja ooc_step_ov8
        cmp ecx, dword ptr [zpatch_ooc_begin]
        jae ooc_step_copy8
        lea eax, [ebx + 1]
    ooc_step_ov8:
        add esi, 5
        mov dword ptr [zpatch_ooc_cursor], eax
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_copy8:
        mov al, byte ptr [ecx]
        lea ecx, [ecx + 1]
        mov byte ptr [edx], al
        inc edx
        sub edi, 1
        jne ooc_step_copy8
        add esi, 5
        mov dword ptr [zpatch_ooc_cursor], edx
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_below6:
        cmp dl, 4
        jne ooc_step_none
        movzx ecx, byte ptr [esi + 4]
        movzx edi, byte ptr [esi + 5]
        movzx eax, byte ptr [esi + 1]
        inc edi
        mov ebx, dword ptr [zpatch_ooc_end]
        shl ecx, 8
        shl eax, 10h
        add edi, ecx
        mov ecx, dword ptr [zpatch_ooc_cursor]
        mov edx, ecx
        sub edx, eax
        movzx eax, byte ptr [esi + 2]
        shl eax, 8
        sub edx, eax
        movzx eax, byte ptr [esi + 3]
        sub edx, eax
        lea eax, [ecx + edi]
        dec edx
        cmp eax, ebx
        ja ooc_step_ov9
        cmp edx, dword ptr [zpatch_ooc_begin]
        jae ooc_step_copy9
        lea eax, [ebx + 1]
    ooc_step_ov9:
        add esi, 6
        mov dword ptr [zpatch_ooc_cursor], eax
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
        nop
    ooc_step_copy9:
        mov al, byte ptr [edx]
        lea edx, [edx + 1]
        mov byte ptr [ecx], al
        inc ecx
        sub edi, 1
        jne ooc_step_copy9
        mov dword ptr [zpatch_ooc_cursor], ecx
        add esi, 6
    ooc_step_none:
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_1004B570, recovered VA 0x1004B570 (159 instructions): the decode driver. ECX
// is the output buffer on entry (`__fastcall`-shaped, which is how the original's
// init calls it: `mov ecx, eax; call`), the answer is 0x2BD50 on success and 0 on
// any failed check. It seeds the stream's four words from the recovered resource,
// steps until the opcode does not advance, then requires the `05 FA` terminator,
// the exact output length and the Adler-32 trailer -- which the original computes
// inline, not through a call, so its loop is transcribed here too: 1872 bytes
// first (the length modulo 5552, constant-folded by the original's compiler), then
// full 5552-byte blocks, each reduced modulo 65521 by the same magic multiply.
//
// The original's init discards the answer, so a resource that failed its own
// checksum is registered and mounted as it decoded, exactly as here.
static __declspec(naked) int __fastcall OocDecodeBody(uint8_t* table)
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 1Ch
        push ebx
        push esi
        push edi
        mov edi, ecx
        mov dword ptr [zpatch_ooc_lower], OFFSET kOocResource - 16
        mov ecx, OFFSET kOocResource
        mov dword ptr [zpatch_ooc_begin], edi
        mov dword ptr [zpatch_ooc_cursor], edi
        lea ebx, [edi + 2BD50h]
        mov dword ptr [zpatch_ooc_end], ebx
        call OocStepBody
        mov dword ptr [ebp - 14h], eax
        cmp eax, OFFSET kOocResource
        je ooc_decode_terminator
    ooc_decode_step:
        cmp dword ptr [zpatch_ooc_cursor], ebx
        ja ooc_decode_fail
        mov ecx, eax
        mov esi, eax
        call OocStepBody
        mov dword ptr [ebp - 14h], eax
        cmp eax, esi
        jne ooc_decode_step
    ooc_decode_terminator:
        cmp byte ptr [eax], 5
        jne ooc_decode_fail
        cmp byte ptr [eax + 1], 0FAh
        jne ooc_decode_fail
        cmp dword ptr [zpatch_ooc_cursor], ebx
        jne ooc_decode_fail
        mov dword ptr [ebp - 10h], 2BD50h
        mov ecx, 1
        xor esi, esi
        mov dword ptr [ebp - 4], 750h
    ooc_decode_block:
        mov dword ptr [ebp - 0Ch], edi
        xor ebx, ebx
    ooc_decode_unroll8:
        movzx eax, byte ptr [edi]
        mov edx, edi
        add ecx, eax
        sub edx, dword ptr [ebp - 0Ch]
        movzx eax, byte ptr [edi + 1]
        add esi, ecx
        add ecx, eax
        add edx, 0Fh
        movzx eax, byte ptr [edi + 2]
        add esi, ecx
        add ecx, eax
        add ebx, 8
        movzx eax, byte ptr [edi + 3]
        add esi, ecx
        add ecx, eax
        movzx eax, byte ptr [edi + 4]
        add esi, ecx
        add ecx, eax
        movzx eax, byte ptr [edi + 5]
        add esi, ecx
        add ecx, eax
        movzx eax, byte ptr [edi + 6]
        add esi, ecx
        add ecx, eax
        movzx eax, byte ptr [edi + 7]
        add esi, ecx
        add ecx, eax
        add edi, 8
        mov eax, dword ptr [ebp - 4]
        add esi, ecx
        cmp edx, eax
        jb ooc_decode_unroll8
        xor edx, edx
        mov dword ptr [ebp - 1Ch], esi
        mov dword ptr [ebp - 0Ch], 0
        cmp ebx, eax
        jae ooc_decode_modulo
        sub eax, ebx
        cmp eax, 2
        jb ooc_decode_single
        mov edx, dword ptr [ebp - 4]
        sub edx, ebx
        sub edx, 2
        shr edx, 1
        inc edx
        lea eax, [ebx + edx*2]
        mov ebx, dword ptr [ebp - 0Ch]
        mov dword ptr [ebp - 18h], eax
        mov esi, ebx
    ooc_decode_pairs:
        movzx eax, byte ptr [edi]
        add ecx, eax
        movzx eax, byte ptr [edi + 1]
        add ebx, ecx
        add edi, 2
        add ecx, eax
        add esi, ecx
        sub edx, 1
        jne ooc_decode_pairs
        mov eax, dword ptr [ebp - 18h]
        mov dword ptr [ebp - 8], esi
        mov esi, dword ptr [ebp - 1Ch]
        cmp eax, dword ptr [ebp - 4]
        jae ooc_decode_pair_tail
        movzx eax, byte ptr [edi]
        mov edx, dword ptr [ebp - 8]
        add ecx, eax
        add esi, ecx
        inc edi
        jmp ooc_decode_combine
    ooc_decode_single:
        movzx eax, byte ptr [edi]
        mov ebx, edx
        add ecx, eax
        add esi, ecx
        inc edi
        jmp ooc_decode_combine
    ooc_decode_pair_tail:
        mov edx, dword ptr [ebp - 8]
    ooc_decode_combine:
        lea eax, [edx + ebx]
        add esi, eax
    ooc_decode_modulo:
        mov eax, 80078071h
        mul ecx
        shr edx, 0Fh
        imul eax, edx, -65521
        add ecx, eax
        mov eax, 80078071h
        mul esi
        shr edx, 0Fh
        imul eax, edx, -65521
        add esi, eax
        mov eax, dword ptr [ebp - 10h]
        sub eax, dword ptr [ebp - 4]
        mov dword ptr [ebp - 10h], eax
        mov dword ptr [ebp - 4], 15B0h
        jne ooc_decode_block
        mov edi, dword ptr [ebp - 14h]
        shl esi, 10h
        add esi, ecx
        movzx edx, byte ptr [edi + 2]
        movzx eax, byte ptr [edi + 3]
        shl edx, 8
        add edx, eax
        movzx eax, byte ptr [edi + 4]
        shl edx, 8
        add edx, eax
        movzx eax, byte ptr [edi + 5]
        shl edx, 8
        add edx, eax
        cmp esi, edx
        jne ooc_decode_fail
        pop edi
        pop esi
        mov eax, 2BD50h
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_decode_fail:
        pop edi
        pop esi
        xor eax, eax
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    }
}

namespace
{
    // One entry of the original's fake-pak vector (a `std::vector` at 0x1016E43C
    // with its three pointers at 0x1016E43C/40/44). The name is a std::string
    // there; the only name ever registered is the eleven-character "finale2.sco",
    // which fits its 16-byte small-string buffer, so the entry keeps that buffer
    // as a fixed array and the layout stays the 44 bytes the builder walks.
    struct OocPakEntry
    {
        char name[16];           // +0x00 the std::string's small-string buffer
        uint32_t name_size;      // +0x10
        uint32_t name_capacity;  // +0x14
        uint32_t file_size;      // +0x18
        uint32_t stored_offset;  // +0x1C
        uint32_t stored_size;    // +0x20
        uint32_t padding;        // +0x24
        uint32_t data;           // +0x28
    };
    static_assert(sizeof(OocPakEntry) == 44, "the original's fake-pak entry is 44 bytes");

    std::vector<OocPakEntry>& OocPakTable()
    {
        static std::vector<OocPakEntry> entries;
        return entries;
    }

    // The archive's 20-byte header and its 16-byte per-entry record
    // (sub_1004AB70, sub_1004AB80, sub_1004ABB0, sub_1004AB90 and the stores
    // sub_1004ACE0 makes into them).
    struct OocHeader
    {
        uint32_t magic;
        uint32_t version;
        uint32_t entries;
        uint32_t names;
        uint16_t page;
        uint16_t opcode;
    };
    static_assert(sizeof(OocHeader) == 20, "the archive header is 20 bytes");

    struct OocRecord
    {
        uint32_t stored_size;
        uint32_t stored_offset;
        uint32_t index;
        uint16_t pages;
        uint16_t padding;  // low 11 bits the pad length, bit 0x20 the offset flag
    };
    static_assert(sizeof(OocRecord) == 16, "the archive record is 16 bytes");

    // sub_1004ABB0: the pages a payload occupies, rounding up.
    uint32_t OocPageCountFor(uint32_t size)
    {
        return size / static_cast<uint32_t>(kOocPageSize) +
               (size % static_cast<uint32_t>(kOocPageSize) != 0u ? 1u : 0u);
    }

    // sub_1004B750's push_back: the name's fields, the payload's size at +0x18 and
    // the table at +0x28. The three offsets the builder recomputes stay zero.
    void OocRegister(uint8_t* table)
    {
        OocPakEntry entry{};
        std::memcpy(entry.name, kOocName, sizeof(kOocName));
        entry.name_size = static_cast<uint32_t>(sizeof(kOocName) - 1);
        entry.name_capacity = static_cast<uint32_t>(sizeof(entry.name) - 1);
        entry.file_size = static_cast<uint32_t>(kOocTableSize);
        entry.data = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(table));
        OocPakTable().push_back(entry);
    }

    // sub_1004ACE0: the in-memory archive. Header, one record per entry, the names,
    // then at 0x800 the payloads with each entry's zero padding to the next page.
    // The builder consumes the payload it was handed (the original frees each
    // entry's block inside the payload loop) and returns a malloc'd archive the
    // caller owns and never frees -- the original leaks it on every call, because
    // the init it runs from re-runs every time.
    uint8_t* OocBuildArchive(uint32_t& final_offset)
    {
        auto& entries = OocPakTable();

        // The original's classification of what to store: a payload of 16 bytes or
        // less, or one that does not start with the compressed-resource dword
        // 0x05435352 ("RSC" + 5), is stored whole at offset 0. This resource starts
        // with "scr" + 0x0E, so it takes that branch.
        for (auto& entry : entries)
        {
            if (entry.file_size <= 0x10u)
            {
                entry.stored_offset = 0;
                entry.stored_size = entry.file_size;
                continue;
            }
            const auto* words = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(entry.data));
            if (words[0] == 0x05435352u)
            {
                entry.stored_offset = words[1];
                entry.stored_size = words[2];
            }
            else
            {
                entry.stored_offset = 0;
                entry.stored_size = entry.file_size;
            }
        }

        uint32_t pages = 0;
        uint32_t names = 0;
        for (const auto& entry : entries)
        {
            pages += OocPageCountFor(entry.file_size);
            names += static_cast<uint32_t>(std::strlen(entry.name) + 1);
        }
        const uint32_t total = pages * static_cast<uint32_t>(kOocPageSize) +
                               static_cast<uint32_t>(kOocPageSize);

        // No null check, like the original's: a failed allocation faults in the
        // memset below.
        auto* archive = static_cast<uint8_t*>(std::malloc(total));
        std::memset(archive, 0, total);

        uint32_t offset = 0;
        const auto append = [&](const void* source, size_t bytes)
        {
            std::memcpy(archive + offset, source, bytes);
            offset += static_cast<uint32_t>(bytes);
        };

        // The header's last two words are the original's fixed 16 and 0xE9.
        const OocHeader header{ kOocArchiveMagic, 3u, static_cast<uint32_t>(entries.size()),
                                names + 16u * static_cast<uint32_t>(entries.size()), 0x10u, 0xE9u };
        append(&header, sizeof(header));

        // The record's index field is the entry's first page, 1-based: the original
        // accumulates each entry's page count into `v9` and stores `v9 + 1`, not the
        // entry's ordinal.
        uint32_t pages_before = 0;
        for (auto& entry : entries)
        {
            OocRecord record{};
            record.stored_size = entry.stored_size;
            record.stored_offset = entry.stored_offset;
            record.index = pages_before + 1u;
            record.pages = static_cast<uint16_t>(OocPageCountFor(entry.file_size));
            const uint32_t remainder = entry.file_size % static_cast<uint32_t>(kOocPageSize);
            if (remainder != 0u)
            {
                entry.padding = static_cast<uint32_t>(kOocPageSize) - remainder;
                record.padding = static_cast<uint16_t>(entry.padding);
                if (entry.stored_offset != 0u)
                    record.padding = static_cast<uint16_t>(record.padding | 0x2000u);
            }
            append(&record, sizeof(record));
            pages_before += record.pages;
        }

        for (const auto& entry : entries)
            append(entry.name, std::strlen(entry.name) + 1);

        // The payload region starts at the first page, whatever the header and the
        // names took (the original resets its offset dword here).
        offset = static_cast<uint32_t>(kOocPageSize);
        for (auto& entry : entries)
        {
            append(reinterpret_cast<const void*>(static_cast<uintptr_t>(entry.data)), entry.file_size);
            std::free(reinterpret_cast<void*>(static_cast<uintptr_t>(entry.data)));
            entry.data = 0;
            if (entry.padding != 0u)
            {
                auto* zeros = static_cast<uint8_t*>(std::malloc(entry.padding));
                std::memset(zeros, 0, entry.padding);
                append(zeros, entry.padding);
                std::free(zeros);
            }
        }

        final_offset = offset;
        return archive;
    }

    // sub_1004B750: decode, register the entry, build the archive, publish the byte
    // count the mount string carries and return the archive. Its one-shot guard
    // dword_10159C00 is read and never written anywhere in the original, so every
    // call re-runs all of this and leaks the previous archive: kept 1:1.
    uint32_t OocInitialise()
    {
        // Unchecked, like the original: the decode writes through the block whether
        // or not the allocation succeeded.
        auto* table = static_cast<uint8_t*>(std::malloc(kOocTableSize));
        static_cast<void>(OocDecodeBody(table));  // the original discards the answer
        OocRegister(table);
        uint32_t final_offset = 0;
        uint8_t* archive = OocBuildArchive(final_offset);
        OocPakTable().clear();  // dword_1016E440 = dword_1016E43C
        zpatch_ooc_state = final_offset;
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(archive));
    }

    // sub_10001280, recovered VA 0x10001280: the bounded formatter, transcribed once
    // in source/stability.ixx and proved there instruction for instruction
    // (docs/rebuilt.md). A module interface cannot export a naked function, so this
    // six-byte forwarder is what the wrapper's `call` names: the original's call is
    // an immediate too, and the forwarder enters the body with the stack and the
    // registers untouched.
    static __declspec(naked) void OocFormat()
    {
        __asm
        {
            jmp DWORD PTR [zpatch_format_entry]
        }
    }

    // fnv1a over a recovered block, for the self-check below only: it is the
    // fingerprint the recovered data's own header records.
    uint32_t OocFnv1a(const uint8_t* data, size_t size)
    {
        uint32_t hash = 2166136261u;
        for (size_t index = 0; index < size; ++index)
        {
            hash ^= data[index];
            hash *= 16777619u;
        }
        return hash;
    }
}

// The offline proof of the three rebuilt transforms: decode the recovered
// resource, register the table, build the archive, and compare each result with
// the fingerprint the recovered bytes must produce. Answers nullptr on success and
// a short description of the first difference otherwise.
//
// The shipped ASI never calls this. The payload path only runs when the game
// calls the wrapper at 0xB70733, which no offline tool can reach, so the decoder
// and the builder would otherwise be unexecuted code; `zp_dump --ooc-selftest`
// runs them against the pinned data instead (docs/parity.md). The two fnv1a
// values are recomputed from `game_refs/ZolikaPatch_original.asi.off` by the
// recipe in source/ooc_finale2.inc's header.
export const char* OocSelfCheck()
{
    auto* table = static_cast<uint8_t*>(std::malloc(kOocTableSize));
    if (table == nullptr)
        return "the decoded table's allocation failed";
    if (OocDecodeBody(table) == 0)
    {
        std::free(table);
        return "the resource did not decode: terminator, output length or Adler-32 trailer";
    }
    const uint32_t table_hash = OocFnv1a(table, kOocTableSize);
    OocRegister(table);
    uint32_t final_offset = 0;
    uint8_t* archive = OocBuildArchive(final_offset);
    OocPakTable().clear();

    const char* failure = nullptr;
    if (table_hash != 0xC883859Fu)
        failure = "the decoded table is not the recovered 179,536 bytes";
    else if (final_offset != 182272u)
        failure = "the archive is not the recovered 182,272 bytes";
    else if (OocFnv1a(archive, final_offset) != 0xB5AF7B10u)
        failure = "the archive is not the recovered layout";
    else if ((archive[16] | (archive[17] << 8)) != 0x10u ||
             (archive[18] | (archive[19] << 8)) != 0xE9u)
        failure = "the archive header's page and opcode words are not 16 and 0xE9";
    std::free(archive);
    return failure;
}

// sub_1004C3C0, recovered VA 0x1004C3C0 (7 instructions): the branch body the E9
// at game 0xA22E97 jumps to. The five bytes it replaces are `cmp [esp+30h], eax`
// plus the first byte of the `movq xmm0, [esp+30h]` that follows, so it replays
// both. If the value is the game's own script magic the flags are that compare's;
// if not, it compares against the archive's 0x86723DFF, and the game's `je` two
// instructions after the resume consumes whichever flags are left. That is what
// makes the archive's resource acceptable to the check the arm widens.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void OocBranchBody()
{
    __asm
    {
        cmp DWORD PTR [esp + 30h], eax
        movq xmm0, QWORD PTR [esp + 30h]
        jne ooc_branch_check
        jmp DWORD PTR [zpatch_ooc_resume]
    ooc_branch_check:
        mov eax, 86723DFFh
        cmp DWORD PTR [esp + 30h], eax
        jmp DWORD PTR [zpatch_ooc_resume]
    }
}

// sub_1004C350, recovered VA 0x1004C350 (41 instructions to its `ret`): the
// wrapper the E8 at game 0xB70733 is retargeted to. It runs the init, formats the
// mount string from the archive's address and byte count, registers it with the
// game's device register (the call at match-0x33 the arm published), and then
// calls the function the site's E8 named before the retarget. The /GS frame is the
// original's: the same cookie, the same 0x44-byte buffer, the same 0x38-byte
// cleanup after the two calls.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void OocWrapBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 44h
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        push esi
        mov esi, DWORD PTR [ebp + 8]
        call OocInitialise
        test eax, eax
        je ooc_wrap_original
        push 0
        push 3Ah
        push 30h
        push DWORD PTR [zpatch_ooc_state]
        push eax
        push 24h
        push 3Ah
        push OFFSET zpatch_ooc_mount_prefix
        push OFFSET zpatch_ooc_mount_format
        lea eax, [ebp - 44h]
        push 40h
        push eax
        call OocFormat
        push -1
        lea eax, [ebp - 44h]
        push 1
        push eax
        call DWORD PTR [zpatch_ooc_mount]
        add esp, 38h
    ooc_wrap_original:
        push DWORD PTR [ebp + 0Ch]
        push esi
        call DWORD PTR [zpatch_ooc_original]
        mov ecx, DWORD PTR [ebp - 4]
        add esp, 8
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
    // The two anchors, as the original handler's own scan strings name them. The
    // first is five bytes starting inside the `mov eax, <magic>` that precedes the
    // resource compare; the second is the device-lookup call the wrapper replaces.
    constexpr std::string_view kOocBranchSignature = "A9 39 44 24 18";
    constexpr std::string_view kOocWrapperSignature =
        "E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56";

    // sub_10001670: the target a `call` at `site` names (`site + 5 + disp32`).
    uint32_t CallTarget(const std::byte* site)
    {
        int32_t displacement{};
        std::memcpy(&displacement, site + 1, sizeof(displacement));
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(site) + 5 + displacement);
    }

    // The publish tier reads what the install tier overwrites: the resume is two
    // instructions past the five bytes the E9 covers, and the wrapper replays the
    // call at the site, so its original target has to be read before the retarget.
    void PublishOutOfCommissionFix()
    {
        if (!Loader::Enabled("OutOfCommissionFix"))
            return;

        auto pattern = find_pattern(kOocBranchSignature);
        if (!pattern.empty())
        {
            zpatch_ooc_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)) + 0x11);
        }

        pattern = find_pattern(kOocWrapperSignature);
        if (!pattern.empty())
        {
            const auto* match = pattern.get_first<std::byte>(0);
            zpatch_ooc_mount = CallTarget(match - 0x33);
            zpatch_ooc_original = CallTarget(match);
        }
    }

    // The install tier: the E9 at the first anchor's +7 and the E8 retarget at the
    // second anchor, which is the original handler's write set on every build its
    // scan hits.
    void InstallOutOfCommissionFix()
    {
        if (!Loader::Enabled("OutOfCommissionFix"))
            return;

        auto pattern = find_pattern(kOocBranchSignature);
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&OocBranchBody), 0x1004C3C0u);
            injector::MakeJMP(pattern.get_first(7), reinterpret_cast<void*>(&OocBranchBody), true);
        }

        pattern = find_pattern(kOocWrapperSignature);
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&OocWrapBody), 0x1004C350u);
            injector::MakeCALL(pattern.get_first(0), reinterpret_cast<void*>(&OocWrapBody), true);
        }
    }
}

// Registered like every other arm: the publish tier reads the two sites the
// install tier overwrites, and the install tier owns the writes. The decoder pair
// carries its recovered VAs too, so a tool that ever branches into it can express
// the branch in the original's coordinates.
export void RegisterOoc()
{
    Bodies::Register(reinterpret_cast<uintptr_t>(&OocStepBody), 0x1004B150u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&OocDecodeBody), 0x1004B570u);

    Loader::PublishEvent() += []
    {
        PublishOutOfCommissionFix();
    };

    Loader::InstallEvent() += []
    {
        InstallOutOfCommissionFix();
    };
}

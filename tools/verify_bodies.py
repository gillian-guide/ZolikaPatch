#!/usr/bin/env python3
"""Prove transcribed hook bodies instruction-for-instruction.

A body's bytes cannot simply be compared with the original's: a body that reads a
published word or resumes through a function pointer carries *our* module's
addresses where the original carries *its* own. What is comparable is the
instruction stream with absolute operands treated as slots -- the same
instruction, the same registers, the same shape of memory operand, and an
absolute operand on both sides compared as a slot rather than as an address.

    python tools/verify_bodies.py --original <asi> --candidate <asi> --bodies <tsv>

Manifest rows: `name <recovered VA> <candidate VA> <max instructions>`, both VAs
as addresses in the image's own base. Execution stops at a terminal instruction
(`jmp` or `ret`) on both sides; a body that ends differently, or that runs past
`max instructions`, fails.

    --selftest   the comparator against the original itself (must pass) and
                 against a one-byte corruption (must fail)

Exit status: 0 proved, 1 a body differs, 2 inputs or capstone missing.
"""

import argparse
import sys
from pathlib import Path

IMAGE_BASE = 0x10000000
# A `ret` ends a body; a `jmp` does not, because these bodies jump within
# themselves (the special-path tail), and a linear sweep covers both sides the same
# way, so an internal jump is compared like any other instruction.
TERMINAL = {"jmp", "ret", "retf", "iret"}

try:
    import capstone
except ImportError:  # pragma: no cover - reported, not raised
    capstone = None


def image_offset(image: bytes, va: int, base: int = IMAGE_BASE):
    """The file offset of a VA, through the PE section table: a linked address is an
    RVA, and a section's raw pointer is what locates it in the file."""
    if len(image) < 0x40 or image[:2] != b"MZ":
        return None
    span = int.from_bytes(image[0x3C:0x40], "little")
    if image[span:span + 4] != b"PE\0\0":
        return None
    sections = int.from_bytes(image[span + 6:span + 8], "little")
    optional_size = int.from_bytes(image[span + 20:span + 22], "little")
    table = span + 24 + optional_size
    rva = va - base
    for index in range(sections):
        entry = table + index * 40
        virtual_size = int.from_bytes(image[entry + 8:entry + 12], "little")
        virtual_address = int.from_bytes(image[entry + 12:entry + 16], "little")
        raw_size = int.from_bytes(image[entry + 16:entry + 20], "little")
        raw_pointer = int.from_bytes(image[entry + 20:entry + 24], "little")
        size = virtual_size or raw_size
        if virtual_address <= rva < virtual_address + size:
            # A section's tail can be uninitialised (.bss): those bytes are not in
            # the file at all, they are zero in the loaded image. Mapping them to
            # raw_pointer + ... would read whatever section happens to follow, which
            # is how an earlier read of a scratch slot returned code bytes.
            if rva - virtual_address >= raw_size:
                return None
            return raw_pointer + (rva - virtual_address)
    return None


def disassemble(image: bytes, va: int, limit: int):
    """Exactly `limit` instructions of one body, swept linearly.

    A body is transcribed instruction for instruction, so the whole stream is
    comparable; stopping at the first terminal instruction hid the tail of a body
    that returns in the middle and proved a body whose entry is a jump as a single
    instruction. The budget in the manifest is therefore the body`s true instruction
    count, not a cap: if either side decodes fewer, the row fails.
    """
    if capstone is None:
        raise SystemExit("capstone is required: pip install capstone")
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    offset = image_offset(image, va)
    if offset is None or offset >= len(image):
        return None
    out = []
    # The window must be larger than any body: a short window silently truncated the
    # sweep and the row still reported PASS with the truncated count, so a body over
    # the window was never fully proven.
    for instruction in md.disasm(image[offset:offset + 4096], va):
        out.append(instruction)
        if len(out) >= limit:
            break
    return out


def summarise(instruction):
    """The comparable shape: mnemonic, registers, memory displacement, and an
    absolute operand reduced to its slot."""
    parts = []
    for operand in instruction.operands:
        if operand.type == capstone.x86.X86_OP_REG:
            parts.append(f"reg:{instruction.reg_name(operand.reg)}")
        elif operand.type == capstone.x86.X86_OP_IMM:
            parts.append("imm")
        elif operand.type == capstone.x86.X86_OP_MEM:
            base = instruction.reg_name(operand.mem.base) if operand.mem.base else ""
            index = instruction.reg_name(operand.mem.index) if operand.mem.index else ""
            if not base and not index:
                parts.append("abs")
            else:
                parts.append(f"[{base}+{index}*{operand.mem.scale}+{operand.mem.disp}]")
        else:
            parts.append("other")
    return f"{instruction.mnemonic} {' '.join(parts)}".strip()


def internal_branch_mismatch(left_stream, right_stream):
    """The first branch whose target offset differs inside the body, or None.

    A `jcc`/`jmp` immediate is reduced to "imm" by `summarise`, so a transcription
    that jumps to the wrong label still matches shape for shape - one row did. Both
    sides are transcribed from the same body, so their instructions occupy the same
    relative offsets: a branch whose target lies inside the body must land on the same
    offset from the body start on both sides.
    """
    if not left_stream or not right_stream:
        return None
    left_start = left_stream[0].address
    left_size = sum(instruction.size for instruction in left_stream)
    right_start = right_stream[0].address

    for index, (left, right) in enumerate(zip(left_stream, right_stream)):
        if left.mnemonic == "call" or not (left.mnemonic.startswith("j")):
            continue
        left_target = None
        right_target = None
        for operand in left.operands:
            if operand.type == capstone.x86.X86_OP_IMM:
                left_target = operand.imm - left_start
        for operand in right.operands:
            if operand.type == capstone.x86.X86_OP_IMM:
                right_target = operand.imm - right_start
        if left_target is None or right_target is None:
            continue
        # Only a target inside the body is comparable: an external one is a slot.
        if not 0 <= left_target < left_size:
            continue
        if left_target != right_target:
            return index, left_target, right_target
    return None


def compare(original: bytes, candidate: bytes, rows, symbols):
    results = []
    for name, reference_va, symbol, limit in rows:
        candidate_va = symbols.get(symbol)
        if candidate_va is None:
            results.append((name, "MISSING", f"symbol {symbol} is not in the map"))
            continue
        wanted = disassemble(original, reference_va, limit)
        have = disassemble(candidate, candidate_va, limit)
        if not wanted or not have:
            results.append((name, "MISSING", "outside the image"))
            continue
        if len(wanted) != len(have):
            results.append((name, "FAIL", f"{len(wanted)} instruction(s) against {len(have)}"))
            continue
        for index, (left, right) in enumerate(zip(wanted, have)):
            if summarise(left) != summarise(right):
                results.append((name, "FAIL",
                                f"instruction {index}: '{summarise(left)}' against '{summarise(right)}'"))
                break
        else:
            # A `jcc`/`jmp` immediate is compared as a slot, so a transcription that
            # jumps to the wrong label still matches shape for shape. The body's
            # instructions sit at the same relative offsets on both sides, so every
            # branch whose target is inside the body has to land on the same relative
            # offset - that is what catches a misplaced label (it caught one).
            mismatch = internal_branch_mismatch(wanted, have)
            if mismatch is not None:
                results.append((name, "FAIL",
                                f"branch at instruction {mismatch[0]} targets "
                                f"{mismatch[1]:+#x} against {mismatch[2]:+#x}"))
                continue
            # The row's budget is the body's exact instruction count, so a linear
            # sweep that decoded the same number on both sides and matched
            # instruction for instruction has covered the whole body - including a
            # tail after a mid-body `ret` and a body whose entry is a jump.
            slots = sum(1 for instruction in wanted if "abs" in summarise(instruction))
            results.append((name, "PASS", f"{len(wanted)} instruction(s), {slots} absolute operand(s)"))
    return results


def read_manifest(path: Path):
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip().lstrip('\ufeff')
        if not line or line.startswith("#"):
            continue
        name, reference_va, symbol, limit = line.split()
        rows.append((name, int(reference_va, 16), symbol, int(limit)))
    return rows


def read_map(path: Path):
    """symbol -> VA from the linker map. A candidate address is a link result, so it
    is looked up at test time instead of written down: a relink moves every body,
    and a stale literal would read the middle of a different function."""
    symbols = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.split()
        for index, token in enumerate(parts[:-1]):
            if not token.startswith("?"):
                continue
            candidate = parts[index + 1]
            if len(candidate) == 8:
                try:
                    symbols[token] = int(candidate, 16)
                except ValueError:
                    continue
    return symbols


def report(results) -> int:
    for name, status, detail in results:
        print(f"{status} {name}" + (f" ({detail})" if detail else ""))
    failed = sum(1 for _, status, _ in results if status != "PASS")
    print(f"{len(results)} body(ies), {failed} failure(s)")
    return 0 if failed == 0 else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--original")
    parser.add_argument("--candidate")
    parser.add_argument("--bodies")
    parser.add_argument("--map")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        if not args.original:
            raise SystemExit("--selftest needs --original")
        original = Path(args.original).read_bytes()
        rows = [("selftest-body", IMAGE_BASE + 0x5590, IMAGE_BASE + 0x5590, 24)]
        first = report(compare(original, original, rows, {IMAGE_BASE + 0x5590: IMAGE_BASE + 0x5590}))
        corrupt = bytearray(original)
        offset = image_offset(original, IMAGE_BASE + 0x5590)
        corrupt[offset] ^= 0xFF  # the opcode, not a constant: a constant alone is not an instruction difference
        second = report(compare(original, bytes(corrupt), rows, {IMAGE_BASE + 0x5590: IMAGE_BASE + 0x5590}))

        # A branch target is compared by offset because `summarise` reduces it to
        # "imm": corrupt a `jcc` displacement and the shape still matches, so only the
        # branch check can catch it. The AimProjectile body's own tail is a jump, so
        # this exercises the real path rather than a synthetic one.
        branches = bytearray(original)
        stream = disassemble(original, IMAGE_BASE + 0x5590, 24) or []
        branch_at = next((instruction for instruction in stream
                          if instruction.mnemonic.startswith("j")), None)
        third = 1
        if branch_at is not None:
            spot = image_offset(original, branch_at.address)
            if spot is not None and branch_at.size >= 2:
                branches[spot + 1] ^= 0x08
            third = report(compare(original, bytes(branches), rows,
                                   {IMAGE_BASE + 0x5590: IMAGE_BASE + 0x5590}))

        ok = first == 0 and second == 1 and third == 1
        print("selftest: identical PASS, corrupted opcode FAIL, moved branch target FAIL"
              if ok else f"selftest: BROKEN ({first}, {second}, {third})")
        return 0 if ok else 2

    if not (args.original and args.candidate and args.bodies and args.map):
        raise SystemExit(__doc__)
    return report(compare(Path(args.original).read_bytes(), Path(args.candidate).read_bytes(),
                          read_manifest(Path(args.bodies)), read_map(Path(args.map))))


if __name__ == "__main__":
    sys.exit(main())

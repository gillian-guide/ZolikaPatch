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

Manifest table rows: `table <recovered VA> <candidate symbol> <bytes>`. They name
a table an arm registers with `Bodies::RegisterTable`: an indexed absolute operand
whose displacement is that table (`push ds:off_100DE770[esi*4]`) is compared as
`table:<content hash>` instead of as a displacement, on both sides, so the proof
asks "does this read the same table?" and not "does the table sit at the same
address?". A displacement that is not a registered table keeps the literal
comparison: the relaxation is opt-in, per table, and an unregistered one behaves
exactly as before.

    --selftest   the comparator against the original itself (must pass) and
                 against a one-byte corruption (must fail), plus the table path:
                 a registered table passes, a changed table *content* fails as
                 `table contents differ`, and an unregistered displacement still
                 compares literally

Exit status: 0 proved, 1 a body differs, 2 inputs or capstone missing.
"""

import argparse
import hashlib
import struct
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


def read_table(image: bytes, va: int, size: int):
    """The registered table's bytes, or None when the image does not hold them.

    The table has to be one contiguous run in the file: a registration that crossed a
    section boundary would otherwise hash whatever bytes happen to follow, and a
    wrong table would compare as a content mismatch rather than as a bad input.
    """
    offset = image_offset(image, va)
    if offset is None or offset + size > len(image):
        return None
    if size and image_offset(image, va + size - 1) != offset + size - 1:
        return None
    return image[offset:offset + size]


def table_content_hash(image: bytes, va: int, size: int):
    """A hash of a registered table's contents that ignores where it sits.

    The table is compared by what it *holds*. A word that addresses the image is
    hashed as the bytes it names (a C string, NUL included), not as the address: a
    module cannot place its string pool where the original's sits, and hashing the
    pointer values would compare the two layouts instead of the two tables. Every
    other word is hashed as it stands, so a table of hashes, floats or ints
    compares by value. `size` is the length the registration names, and it is
    exactly what is hashed.
    """
    data = read_table(image, va, size)
    if data is None:
        return None
    canonical = bytearray()
    whole = len(data) - len(data) % 4
    for offset in range(0, whole, 4):
        word = int.from_bytes(data[offset:offset + 4], "little")
        target = image_offset(image, word)
        if target is None:
            canonical += b"v" + data[offset:offset + 4]
        else:
            end = image.find(b"\0", target, target + 0x1000)
            if end < 0:
                end = target + 0x1000
            canonical += b"s" + image[target:end]
        canonical += b"\0"
    canonical += b"t" + data[whole:]
    return hashlib.sha256(bytes(canonical)).hexdigest()[:16]


class Tables:
    """The registered tables on one side of a comparison.

    `entries` maps each table's VA *in that side's image* to the length the
    registration names; the original side gets the recovered VA, the candidate side
    the VA the linker map gives the candidate's own copy. The hashes are computed
    on first use, because most rows read no table at all.
    """

    def __init__(self, image: bytes, entries, side: str):
        self.image = image
        self.entries = entries
        self.side = side
        self._hashes = {}

    def resolve(self, displacement: int):
        """The content hash of the registered table at `displacement`, or None.

        Only an exact registered VA resolves: a displacement that is merely near a
        table is not that table, and an operand the registry does not name keeps
        the literal comparison.
        """
        va = displacement & 0xFFFFFFFF
        size = self.entries.get(va)
        if size is None:
            return None
        if va not in self._hashes:
            digest = table_content_hash(self.image, va, size)
            if digest is None:
                raise SystemExit(
                    f"registered table {va:#x} ({self.side} side) is not in the image")
            self._hashes[va] = digest
        return self._hashes[va]


def summarise(instruction, tables=None):
    """The comparable shape: mnemonic, registers, memory displacement, and an
    absolute operand reduced to its slot.

    An indexed operand with no base register is an indexed absolute: its
    displacement names a table. When `tables` resolves it, the displacement is
    compared as `table:<content hash>` and the index register and scale are still
    compared, so the same table read through a different register still fails. A
    displacement the registry does not name keeps the literal comparison.
    """
    parts = []
    for operand in instruction.operands:
        if operand.type == capstone.x86.X86_OP_REG:
            parts.append(f"reg:{instruction.reg_name(operand.reg)}")
        elif operand.type == capstone.x86.X86_OP_IMM:
            parts.append("imm")
        elif operand.type == capstone.x86.X86_OP_MEM:
            base = instruction.reg_name(operand.mem.base) if operand.mem.base else ""
            index = instruction.reg_name(operand.mem.index) if operand.mem.index else ""
            if not base and index and tables is not None:
                table = tables.resolve(operand.mem.disp)
                if table is not None:
                    parts.append(f"[{index}*{operand.mem.scale}+table:{table}]")
                    continue
            if not base and not index:
                parts.append("abs")
            else:
                parts.append(f"[{base}+{index}*{operand.mem.scale}+{operand.mem.disp}]")
        else:
            parts.append("other")
    return f"{instruction.mnemonic} {' '.join(parts)}".strip()


def table_operand_count(stream, tables) -> int:
    """How many indexed absolute operands the stream resolves to a table."""
    if tables is None:
        return 0
    count = 0
    for instruction in stream:
        for operand in instruction.operands:
            if (operand.type == capstone.x86.X86_OP_MEM and not operand.mem.base
                    and operand.mem.index and tables.resolve(operand.mem.disp) is not None):
                count += 1
    return count


def table_mismatch(left, right, left_tables, right_tables):
    """The specific answer when a differing instruction is a table read.

    Two registered tables whose contents differ is not an instruction difference:
    the transcription reads a different table. A table that resolves on one side
    and not the other is the registration (or the displacement) being wrong, and is
    reported as such.
    """
    for left_operand, right_operand in zip(left.operands, right.operands):
        if (left_operand.type != capstone.x86.X86_OP_MEM
                or right_operand.type != capstone.x86.X86_OP_MEM):
            continue
        if left_operand.mem.base or right_operand.mem.base:
            continue
        if not left_operand.mem.index or not right_operand.mem.index:
            continue
        left_hash = left_tables.resolve(left_operand.mem.disp) if left_tables else None
        right_hash = right_tables.resolve(right_operand.mem.disp) if right_tables else None
        if left_hash is not None and right_hash is not None:
            if left_hash != right_hash:
                return (f"table contents differ: {left_operand.mem.disp & 0xFFFFFFFF:#x} "
                        f"({left_hash}) against {right_operand.mem.disp & 0xFFFFFFFF:#x} "
                        f"({right_hash})")
        elif left_hash is not None or right_hash is not None:
            side, va = ((("original", left_operand.mem.disp) if left_hash is not None
                         else ("candidate", right_operand.mem.disp)))
            return f"table {va & 0xFFFFFFFF:#x} is registered on the {side} side only"
    return None


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


def compare(original: bytes, candidate: bytes, rows, symbols,
            original_tables=None, candidate_tables=None):
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
            if summarise(left, original_tables) != summarise(right, candidate_tables):
                # A registered table read that differs is reported as a table
                # difference: "the transcription reads another table" is the
                # finding, not "instruction 5 differs".
                detail = table_mismatch(left, right, original_tables, candidate_tables)
                results.append((name, "FAIL",
                                detail or f"instruction {index}: "
                                          f"'{summarise(left, original_tables)}' against "
                                          f"'{summarise(right, candidate_tables)}'"))
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
            tables = table_operand_count(wanted, original_tables)
            detail = f"{len(wanted)} instruction(s), {slots} absolute operand(s)"
            if tables:
                detail += f", {tables} table operand(s)"
            results.append((name, "PASS", detail))
    return results


def read_manifest(path: Path):
    """The manifest's body rows and its table rows.

    A body row is `name <recovered VA> <candidate symbol> <instruction count>`; a
    table row is `table <recovered VA> <candidate symbol> <bytes>`. A table row is
    the offline projection of the arm's `Bodies::RegisterTable` call: the
    recovered VA and length are the ones the registration names, and the candidate
    symbol is how the linker map gives this module's own copy its address.
    """
    rows = []
    tables = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip().lstrip('\ufeff')
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if parts[0] == "table":
            _, reference_va, symbol, size = parts
            tables.append((int(reference_va, 16), symbol, int(size)))
            continue
        name, reference_va, symbol, limit = parts
        rows.append((name, int(reference_va, 16), symbol, int(limit)))
    return rows, tables


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

        # The table path. sub_10003F00 (21 instructions) reads the 38,455-entry name
        # table at 0x100DE770 through two indexed absolute operands, which is the
        # class-A blocker: the displacement cannot match, so the table is registered
        # and compared by content. The four cases are: registered and identical ->
        # PASS; same table, one changed name -> FAIL as `table contents differ`; the
        # operand moved to a *different* registered table -> FAIL as a content
        # difference (not an instruction one); and the same moved operand with no
        # registration -> FAIL literally, which is what keeps the relaxation opt-in.
        name_body = IMAGE_BASE + 0x3F00
        name_rows = [("selftest-table", name_body, name_body, 21)]
        name_symbols = {name_body: name_body}
        table_va = IMAGE_BASE + 0xDE770
        table_bytes = 0x9637 * 4
        registered = Tables(original, {table_va: table_bytes}, "original")
        fourth_results = compare(original, original, name_rows, name_symbols,
                                 registered, registered)
        fourth = report(fourth_results)
        # The PASS has to be the *table* path, not two identical images sliding
        # through: the row's detail counts the operands the registry resolved.
        resolved = any("table operand" in detail for _, _, detail in fourth_results)

        changed = bytearray(original)
        first_name = int.from_bytes(read_table(original, table_va, 4), "little")
        name_offset = image_offset(original, first_name)
        if name_offset is not None:
            changed[name_offset] ^= 0x20
        fifth = report(compare(original, bytes(changed), name_rows, name_symbols,
                               registered,
                               Tables(bytes(changed), {table_va: table_bytes}, "candidate")))

        # 0x10003F13 is the first table operand's displacement (the `push` at
        # 0x10003F10). Point it at the "**UNKNOWN**" literal instead, and register
        # that VA as a table of its own: both sides resolve, the contents differ.
        moved = bytearray(original)
        body_offset = image_offset(original, name_body)
        unknown_va = IMAGE_BASE + 0x128D20
        if body_offset is not None:
            struct.pack_into("<I", moved, body_offset + 0x13, unknown_va)
        sixth = report(compare(original, bytes(moved), name_rows, name_symbols,
                               registered,
                               Tables(bytes(moved), {unknown_va: 12}, "candidate")))
        seventh = report(compare(original, bytes(moved), name_rows, name_symbols))

        ok = (first == 0 and second == 1 and third == 1 and fourth == 0 and resolved
              and fifth == 1 and sixth == 1 and seventh == 1)
        print("selftest: identical PASS, corrupted opcode FAIL, moved branch target FAIL, "
              "registered table PASS (2 operands resolved), changed table contents FAIL, "
              "moved table operand FAIL, unregistered table operand FAIL"
              if ok else f"selftest: BROKEN ({first}, {second}, {third}, {fourth}, "
                         f"{resolved}, {fifth}, {sixth}, {seventh})")
        return 0 if ok else 2

    if not (args.original and args.candidate and args.bodies and args.map):
        raise SystemExit(__doc__)
    original = Path(args.original).read_bytes()
    candidate = Path(args.candidate).read_bytes()
    rows, table_rows = read_manifest(Path(args.bodies))
    symbols = read_map(Path(args.map))
    candidate_tables = {}
    for reference_va, symbol, size in table_rows:
        candidate_va = symbols.get(symbol)
        if candidate_va is None:
            raise SystemExit(f"table symbol {symbol} is not in the map")
        candidate_tables[candidate_va] = size
    return report(compare(original, candidate, rows, symbols,
                          Tables(original, {va: size for va, _, size in table_rows}, "original"),
                          Tables(candidate, candidate_tables, "candidate")))


if __name__ == "__main__":
    sys.exit(main())

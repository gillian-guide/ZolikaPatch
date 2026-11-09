#!/usr/bin/env python3
"""Disassemble a VA range of an original ASI/EXE via capstone (offline, no IDA).

Usage: python s9_disasm.py <file> <va> [count] [--bytes N]
  file  : path to a PE (e.g. game_refs/ZolikaPatch_original.asi.off)
  va    : hex VA (image base from the PE header)
  count : number of instructions (default 40)
"""
from __future__ import annotations

import pathlib
import sys

from capstone import CS_ARCH_X86, CS_MODE_32, Cs


def sections(path: pathlib.Path):
    data = path.read_bytes()
    pe = int.from_bytes(data[0x3C:0x40], "little")
    coff = pe + 4
    nsec = int.from_bytes(data[coff + 2 : coff + 4], "little")
    opt_size = int.from_bytes(data[coff + 16 : coff + 18], "little")
    opt = coff + 20
    image_base = int.from_bytes(data[opt + 28 : opt + 32], "little")
    sec_off = opt + opt_size
    secs = []
    for i in range(nsec):
        base = sec_off + i * 40
        name = data[base : base + 8].rstrip(b"\0").decode("latin1")
        vsize = int.from_bytes(data[base + 8 : base + 12], "little")
        va = int.from_bytes(data[base + 12 : base + 16], "little")
        rawptr = int.from_bytes(data[base + 20 : base + 24], "little")
        secs.append((name, va, vsize, rawptr))
    return data, image_base, secs


def va_to_off(image_base, secs, va):
    rva = va - image_base
    for _name, sva, vsize, rawptr in secs:
        if sva <= rva < sva + vsize:
            return rawptr + (rva - sva)
    return None


def main() -> int:
    path = pathlib.Path(sys.argv[1])
    va = int(sys.argv[2], 16)
    count = int(sys.argv[3]) if len(sys.argv) > 3 and not sys.argv[3].startswith("-") else 40
    data, image_base, secs = sections(path)
    off = va_to_off(image_base, secs, va)
    if off is None:
        print(f"VA {va:#x} not mapped")
        return 2
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = False
    n = 0
    for insn in md.disasm(data[off : off + 3000], va):
        raw = " ".join(f"{b:02X}" for b in insn.bytes)
        print(f"{insn.address:08X}  {insn.mnemonic:9s} {insn.op_str:42s} ; {raw}")
        n += 1
        if n >= count:
            break
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

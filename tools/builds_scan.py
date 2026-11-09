#!/usr/bin/env python3
"""Scan patterns across every game build available in game_refs.

Usage:
  python builds_scan.py <pattern> [<pattern> ...]
Patterns are hex bytes with ?/?? wildcards. Prints hits per build. VAs are
computed through the PE section table (GTAIV_12059.exe has file offset == VA,
the other exes have file offset == VA - 0x400000).
"""
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
GAME_REFS = ROOT / "game_refs"

BUILDS = [
    ("1000", GAME_REFS / "GTAIV_1000.exe"),
    ("1010", GAME_REFS / "GTAIV_1010.exe"),
    ("1004", GAME_REFS / "GTAIV_1004.exe"),
    ("1040", GAME_REFS / "GTAIV_1040.exe"),
    ("1060", GAME_REFS / "GTAIV_1060.exe"),
    ("1061", GAME_REFS / "GTAIV_1061.exe"),
    ("1070", GAME_REFS / "GTAIV_1070.exe"),
    ("1080", GAME_REFS / "GTAIV.exe"),
    ("12059", GAME_REFS / "GTAIV_12059.exe"),
    ("eflc1100", GAME_REFS / "EFLC_1100.exe"),
    ("eflc1110", GAME_REFS / "EFLC_1110.exe"),
    ("eflc1112", GAME_REFS / "EFLC_1112.exe"),
    ("eflc1120", GAME_REFS / "EFLC_1120.exe"),
    ("eflc1130", GAME_REFS / "EFLC_1130.exe"),
]


def compile_pattern(text: str):
    raw, mask = [], []
    for tok in text.split():
        if tok in ("?", "??"):
            raw.append(0)
            mask.append(False)
        else:
            raw.append(int(tok, 16))
            mask.append(True)
    return bytes(raw), mask


def pe_sections(data: bytes):
    pe = int.from_bytes(data[0x3C:0x40], "little")
    coff = pe + 4
    nsec = int.from_bytes(data[coff + 2 : coff + 4], "little")
    opt_size = int.from_bytes(data[coff + 16 : coff + 18], "little")
    opt = coff + 20
    image_base = int.from_bytes(data[opt + 28 : opt + 32], "little")
    sec_off = opt + opt_size
    secs = []
    for i in range(nsec):
        b = sec_off + i * 40
        va = int.from_bytes(data[b + 12 : b + 16], "little")
        vsize = int.from_bytes(data[b + 8 : b + 12], "little")
        rawptr = int.from_bytes(data[b + 20 : b + 24], "little")
        rawsize = int.from_bytes(data[b + 16 : b + 20], "little")
        secs.append((va, vsize, rawptr, rawsize))
    return image_base, secs


def offset_to_va(image_base, secs, off: int) -> int | None:
    for va, vsize, rawptr, rawsize in secs:
        if rawptr <= off < rawptr + max(rawsize, 1):
            return image_base + va + (off - rawptr)
    return None


def scan(data: bytes, raw: bytes, mask, limit=6):
    hits = []
    n = len(raw)
    first = raw[0]
    pos = 0
    while True:
        i = data.find(bytes([first]), pos)
        if i < 0 or i + n > len(data):
            break
        ok = True
        for j in range(1, n):
            if mask[j] and data[i + j] != raw[j]:
                ok = False
                break
        if ok:
            hits.append(i)
            if len(hits) >= limit:
                break
        pos = i + 1
    return hits


def main() -> int:
    patterns = sys.argv[1:]
    if not patterns:
        print("usage: builds_scan.py <pattern> ...")
        return 2
    compiled = [(p, *compile_pattern(p)) for p in patterns]
    for name, path in BUILDS:
        if not path.exists():
            print(f"{name:10s} (missing: {path.name})")
            continue
        data = path.read_bytes()
        image_base, secs = pe_sections(data)
        out = []
        for pat, raw, mask in compiled:
            hits = scan(data, raw, mask)
            va = offset_to_va(image_base, secs, hits[0]) if hits else None
            first = f"{va:#010x}" if va is not None else ("?" if hits else "-")
            out.append(f"{len(hits)}{'!' if len(hits) >= 6 else ''}@{first}")
        print(f"{name:10s} " + "  ".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())

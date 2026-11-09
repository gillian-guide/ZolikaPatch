#!/usr/bin/env python3
"""Verify the generated module-rva install rows against the pristine per-build
game executables in game_refs/.

For every row of evidence/rva_installs.json with a non-zero site_rva and a
build other than 9 (1.0.8.0), the matching exe is checked:

  (a) site_rva + 8 <= file size (the file offset equals the RVA: these images
      have image base 0x400000 and, for the checked builds, .text's
      VirtualAddress equals its PointerToRawData; a section-mapped offset is
      computed and must agree),
  (b) the 8 bytes at the site are not all 0x00 and not all 0xCC (real code,
      not padding/uninitialised space).

It also reports conflicts: the same handler+site recovered in two builds with
different opcode/destination (each keeps its own row - informational).

Usage:
    python ZolikaPatch_reconstruction/tools/verify_rva_installs.py
"""
from __future__ import annotations

import json
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
REPO = ROOT.parent
GAME_REFS = REPO / "game_refs"
EVIDENCE = ROOT / "evidence"

# Build ordinal -> pristine exe (the task's mapping).
BUILD_EXE = {
    1: "GTAIV_1000.exe",
    5: "GTAIV_1040.exe",
    6: "GTAIV_1004.exe",
    7: "GTAIV_1060.exe",
    8: "GTAIV_1070.exe",
    9: "GTAIV.exe",
    11: "EFLC_1110.exe",
    12: "EFLC_1120.exe",
    13: "EFLC_1130.exe",
    18: "GTAIV_12059.exe",
}


def sections(path: pathlib.Path):
    data = path.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt = pe + 24
    imgbase = struct.unpack_from("<I", data, opt + 28)[0]
    off = opt + struct.unpack_from("<H", data, pe + 20)[0]
    out = []
    for i in range(nsec):
        s = off + i * 40
        name = data[s:s + 8].rstrip(b"\x00").decode(errors="replace")
        vsz, va, rsz, raw = struct.unpack_from("<IIII", data, s + 8)
        out.append((name, va, max(vsz, rsz), raw))
    return data, imgbase, out


def rva_to_offset(sects, rva: int):
    for name, va, size, raw in sects:
        if va <= rva < va + size:
            return raw + (rva - va), name
    return None, None


def main() -> int:
    rows = json.loads((EVIDENCE / "rva_installs.json").read_text(encoding="utf-8"))
    checked = 0
    failures = []
    unverifiable = []
    images = {}

    for row in rows:
        site = row.get("site_rva")
        build = row["build"]
        if not site or build == 9:
            continue
        exe = BUILD_EXE.get(build)
        if exe is None:
            unverifiable.append((row, f"build {build} has no pristine exe"))
            continue
        path = GAME_REFS / exe
        if path not in images:
            images[path] = sections(path)
        data, imgbase, sects = images[path]
        offset, secname = rva_to_offset(sects, site)
        checked += 1
        tag = (f"{row['option']} [build {build}] site={site:#x} "
               f"({row['destination_kind']}={row.get('destination')}) in {exe}")
        if offset is None:
            failures.append(f"{tag}: RVA not inside any section")
            continue
        if offset != site:
            failures.append(f"{tag}: section-mapped offset {offset:#x} != RVA "
                            f"(raw!=VA mapping; row was written as file offset == RVA)")
            continue
        if site + 8 > len(data):
            failures.append(f"{tag}: site+8 > file size {len(data):#x}")
            continue
        chunk = data[site:site + 8]
        if all(b == 0x00 for b in chunk):
            failures.append(f"{tag}: bytes {chunk.hex(' ')} are all 0x00")
            continue
        if all(b == 0xCC for b in chunk):
            failures.append(f"{tag}: bytes {chunk.hex(' ')} are all 0xCC")
            continue
        print(f"ok   {tag}: {chunk.hex(' ')}")

    # Conflicts: same handler+site in two builds with different write content.
    by_site = {}
    for row in rows:
        if not row.get("site_rva"):
            continue
        key = (row["handler"], row["site_rva"])
        by_site.setdefault(key, []).append(row)
    conflicts = []
    for key, group in sorted(by_site.items()):
        shapes = {(r["build"], r["opcode"], r["destination_kind"], r.get("destination"))
                  for r in group}
        builds = {s[0] for s in shapes}
        if len(builds) > 1:
            per_build = {}
            for b, op, kind, dest in shapes:
                per_build.setdefault(b, []).append((op, kind, dest))
            distinct = {tuple(sorted(v)) for v in per_build.values()}
            if len(distinct) > 1:
                conflicts.append((key, per_build))

    print()
    print(f"rows checked: {checked}")
    if unverifiable:
        for row, why in unverifiable:
            print(f"unverifiable: {row['option']} [build {row['build']}] "
                  f"site={row['site_rva']:#x}: {why}")
    if conflicts:
        print()
        print("conflicts (same handler+site, different per-build writes):")
        for (handler, site), per_build in conflicts:
            print(f"  handler {handler} site {site:#x}:")
            for b in sorted(per_build):
                print(f"    build {b}: {per_build[b]}")
    if failures:
        print()
        for f in failures:
            print(f"FAIL: {f}", file=sys.stderr)
        return 1
    print(f"PASS: {checked} non-build-9 rows verified against the per-build exes"
          + (f"; {len(unverifiable)} unverifiable" if unverifiable else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())

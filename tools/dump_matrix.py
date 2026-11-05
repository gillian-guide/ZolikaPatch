#!/usr/bin/env python3
"""The 18-cell dump matrix: 9 builds x (full, per-option) against the baselines.

This is the acceptance measurement for the write set. It runs the dump for every
supported image in both modes and compares each cell with the stored baseline,
reporting per build how many bytes match and which keys are still open. It does
not fail the build: the rewrite is incomplete, and this tool's job is to say
exactly how incomplete.

    python tools/dump_matrix.py [--baseline DIR] [--only 9]
"""

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILDS = {1: "GTAIV_1000", 5: "GTAIV_1040", 6: "GTAIV_1004", 7: "GTAIV_1060",
          8: "GTAIV_1070", 9: "GTAIV", 11: "EFLC_1110", 12: "EFLC_1120", 13: "EFLC_1130"}


def rows(path, key=None):
    found = {}
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.split()
        if len(parts) == 2:
            found[int(parts[0], 16)] = int(parts[1], 16)
        elif len(parts) == 3 and (key is None or parts[0] == key):
            found[int(parts[1], 16)] = int(parts[2], 16)
    return found


def keys():
    table = ROOT / "source" / "options.inc"
    return [line.split("(", 1)[1].split(",", 1)[0]
            for line in table.read_text(encoding="utf-8").splitlines()
            if line.startswith("ZP_OPTION(")]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", default=str(ROOT.parent / "readable" / "build" / "p3-final-dumps3"))
    parser.add_argument("--only")
    # The full cell is the write set of the shipped INI, so it has to be the same
    # document the baseline was produced from. Per-option cells rebuild themselves
    # (every key off, one key on) and ignore this file.
    parser.add_argument("--ini", default=str(ROOT / "ZolikaPatch.ini"))
    args = parser.parse_args()

    baseline = Path(args.baseline)
    dump = ROOT / "build" / "win-x86" / "Release" / "zp_dump.exe"
    ini = Path(args.ini).resolve()
    if not ini.exists():
        raise SystemExit(f"the shipped INI is missing: {ini}")

    builds = {int(args.only): BUILDS[int(args.only)]} if args.only else BUILDS
    print(f"{'build':>6} {'full':>18} {'per-option':>22}")
    for build, image in builds.items():
        exe = ROOT.parent / "game_refs" / f"{image}.exe"
        if not exe.exists():
            print(f"{build:>6}  missing image")
            continue
        out = ROOT / "build" / "win-x86" / f"matrix-b{build}.txt"
        subprocess.run([str(dump), "--exe", str(exe), "--build", str(build), str(out), str(ini)],
                       check=True, capture_output=True)
        ours_full = rows(out)
        want_full = rows(baseline / f"b{build}-full.txt")
        agreed = len(set(want_full) & set(ours_full))
        differing = sum(1 for rva in set(want_full) & set(ours_full) if want_full[rva] != ours_full[rva])

        subprocess.run([str(dump), "--per-option", "--exe", str(exe), "--build", str(build), str(out),
                        str(ini)], check=True, capture_output=True)
        closed = 0
        for key in keys():
            want = rows(baseline / f"b{build}-per.txt", key)
            have = rows(out, key)
            if not want and not have:
                closed += 1
            elif want == have:
                closed += 1
        print(f"{build:>6} {agreed:>6}/{len(want_full):<6} diff {differing:<2} "
              f"{closed:>6}/{len(keys())} key(s) exact")
        out.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())

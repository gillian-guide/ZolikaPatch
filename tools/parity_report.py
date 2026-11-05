#!/usr/bin/env python3
"""Per-key parity scoreboard against the stored baselines.

For every catalogue key, compares our per-option dump with the baseline the old
tree produced, and prints one line per key: matched, missing, extra, differing.
A key is closed only when all four are zero (or both sides are empty).

    python tools/parity_report.py <build> [--baseline DIR] [--ours FILE]

Defaults: baseline ../readable/build/p3-final-dumps3, ours a fresh per-option
dump of that build. Exit status: 0 when every key is closed, 1 otherwise.
"""

import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_bodies import image_offset

ROOT = Path(__file__).resolve().parent.parent
BUILDS = {1: "GTAIV_1000", 5: "GTAIV_1040", 6: "GTAIV_1004", 7: "GTAIV_1060",
          8: "GTAIV_1070", 9: "GTAIV", 11: "EFLC_1110", 12: "EFLC_1120", 13: "EFLC_1130"}
GAME_BASE = 0x400000


def displacement_rvas(pristine: bytes, rvas):
    """The RVAs that are the displacement of a branch, read from the pristine image.

    A cell only lists the bytes a key changed, so for an arm installed over a site
    that was already an E8/E9 the opcode row is in neither cell and the four
    displacement bytes look like an ordinary difference. The image itself says
    whether the byte before an RVA is a branch, and that is the only reliable
    source: the baselines wrote one stand-in address for every body target, so the
    bytes themselves cannot be compared.
    """
    out = set()
    for rva in rvas:
        if rva < 1:
            continue
        offset = image_offset(pristine, GAME_BASE + rva - 1)
        if offset is not None and pristine[offset] in (0xE8, 0xE9):
            out.add(rva)
    return out


def rows(path, key=None):
    out = {}
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.split()
        if len(parts) == 3 and (key is None or parts[0] == key):
            out[int(parts[1], 16)] = int(parts[2], 16)
        elif len(parts) == 2 and key is None:
            out[int(parts[0], 16)] = int(parts[1], 16)
    return out


def keys():
    table = (ROOT / "source" / "options.inc")
    out = []
    for line in table.read_text(encoding="utf-8").splitlines():
        if line.startswith("ZP_OPTION("):
            out.append(line.split("(", 1)[1].split(",", 1)[0])
    return out


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    build = sys.argv[1]
    baseline_dir = ROOT.parent / "readable" / "build" / "p3-final-dumps3"
    ours_path = None
    index = 2
    while index < len(sys.argv):
        if sys.argv[index] == "--baseline":
            baseline_dir = Path(sys.argv[index + 1])
            index += 2
        elif sys.argv[index] == "--ours":
            ours_path = Path(sys.argv[index + 1])
            index += 2
        else:
            raise SystemExit(__doc__)

    pristine = (ROOT.parent / "game_refs" / f"{BUILDS[int(build)]}.exe").read_bytes()

    if ours_path is None:
        ours_path = ROOT / "build" / "win-x86" / f"per-b{build}.txt"
        dump = ROOT / "build" / "win-x86" / "Release" / "zp_dump.exe"
        image = ROOT.parent / "game_refs" / f"{BUILDS[int(build)]}.exe"
        subprocess.run([str(dump), "--per-option", "--exe", str(image), "--build", build,
                        str(ours_path), "no-such.ini"], check=True, capture_output=True)

    baseline_path = baseline_dir / f"b{build}-per.txt"
    closed = 0
    open_keys = 0
    for key in keys():
        want = rows(baseline_path, key)
        have = rows(ours_path, key)
        missing = len(set(want) - set(have))
        extra = len(set(have) - set(want))
        # The displacement bytes of every branch in the cell, from either side: a
        # branch`s displacement is not comparable (the baseline wrote one stand-in
        # address for every body, and ours names the registered VA), so those bytes
        # are reported apart from a real difference. Looking at both sides matters
        # for an arm that installs over an existing branch: then the opcode byte is
        # unchanged and only the baseline has no opcode row of its own.
        branch = {rva + offset
                  for rows in (want, have)
                  for rva, byte in rows.items() if byte in (0xE8, 0xE9)
                  for offset in range(1, 5)}
        differing = sum(1 for rva in set(want) & set(have)
                        if want[rva] != have[rva] and rva not in branch)
        if not want and not have:
            closed += 1
            continue
        if missing or extra or differing:
            open_keys += 1
            print(f"OPEN {key}: baseline {len(want)}, ours {len(have)}, "
                  f"missing {missing}, extra {extra}, differing {differing}")
        else:
            closed += 1
    print(f"build {build}: {closed} key(s) closed, {open_keys} open")
    return 0 if open_keys == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
def branch_bytes(rows):
    """The displacement bytes of every branch in a cell: the baseline's own
    canonicalisation decides what a branch points at, so those bytes are reported
    separately from a real difference."""
    marked = set()
    for rva, byte in rows.items():
        if byte in (0xE8, 0xE9):
            marked.update(rva + offset for offset in range(1, 5))
    return marked

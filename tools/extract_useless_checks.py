"""Recover the RemoveUselessChecks mass-patch sites (finding N5).

The original's loader (sub_100523C0, arm gated on `RemoveUselessChecks == 1`,
default "0") calls sub_10044BD0, which walks per-build static RVA tables in the
ASI's .rdata and writes five bytes at game_base + entry for every entry - the
bulk is `90 90 90 90 90` (5-byte NOPs) plus a few `B0 01 90 90 90` and other
small patches. The phase-3 op extractor recovered only the non-loop writes, so
the port used to leave ~2,200-2,500 table-driven sites per affected build
(1040/1070/1080) unpatched.

This tool reads the per-build oracle cells
(`cell_<build>_RemoveUselessChecks_1_o.json` from the phase-3 scratch, produced
by tools/phase3/run_config_sweep.py) and emits:

  * `ZolikaPatch_reconstruction/src/recovered_useless_checks.inc` - one
    {build, site_rva, length, bytes[]} row per written site (the original's
    4-byte + optional 1-byte store pair), excluding ranges already covered by
    the port's existing RemoveUselessChecks rows;
  * `ZolikaPatch_reconstruction/evidence/useless_checks.json` - counts and a
    content hash for the audit trail.

Usage:
  python ZolikaPatch_reconstruction/tools/extract_useless_checks.py \
      [--cells <dir>] [--check]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RECON = os.path.abspath(os.path.join(HERE, ".."))
SRC = os.path.join(RECON, "src")
EVIDENCE = os.path.join(RECON, "evidence")

DEFAULT_CELLS = r"C:\Users\Gillian\AppData\Local\Temp\kilo\p3_cells"
CELL_BUILDS = {
    "1000": 1, "1004": 6, "1040": 5, "1060": 7, "1070": 8, "1080": 9,
    "1110": 11, "1120": 12, "1130": 13,
}
HANDLER_LO = 0x10044BD0
HANDLER_HI = 0x10045400
GAME_BASE = 0x400000
# The handler also pokes a couple of non-game buffers (e.g. 0xFD6B004 in the
# harness); only rvas inside any GTAIV image (<32 MB, real ones top out ~26 MB)
# are patchable game sites.
GAME_LIMIT = 0x2000000


def existing_covered_ranges() -> list[tuple[int, int]]:
    """Ranges the port already patches for RemoveUselessChecks (rva fills and
    op rows), so the new table only carries what was missing."""
    ranges: list[tuple[int, int]] = []
    inc = os.path.join(SRC, "recovered_rva_installs.inc")
    for line in open(inc, encoding="utf-8"):
        m = re.match(
            r'\{"RemoveUselessChecks", (\d+)u, (0x[0-9a-fA-F]+)u, 2u, \d+u, (\d+)u,',
            line.strip())
        if m:
            ranges.append((int(m.group(1)), int(m.group(3))))
    ops = os.path.join(SRC, "recovered_patch_ops.inc")
    for line in open(ops, encoding="utf-8"):
        if '"RemoveUselessChecks"' not in line:
            continue
        m = re.match(r'\{"RemoveUselessChecks", "", 0, (\d+)u, "([0-9A-F ]+)", (\d+)u,',
                     line.strip())
        if m:
            ranges.append((int(m.group(3)), int(m.group(1))))
    return ranges


def covered(ranges: list[tuple[int, int]], site: int, length: int) -> bool:
    return any(site < rva + ln and rva < site + length for rva, ln in ranges)


def collect(cells_dir: str) -> dict[int, list[tuple[int, bytes]]]:
    """Per build: (site_rva, bytes) for every handler write, coalescing the
    original's 4-byte + 1-byte store pairs into one 5-byte row."""
    sites: dict[int, list[tuple[int, bytes]]] = {}
    for name, ordinal in CELL_BUILDS.items():
        path = os.path.join(cells_dir, f"cell_{name}_RemoveUselessChecks_1_o.json")
        if not os.path.exists(path):
            raise SystemExit(f"missing cell: {path}")
        data = json.load(open(path))
        four: dict[int, int] = {}
        one: dict[int, int] = {}
        for va, size, value, eip, _ret in data["writes"]:
            if not (HANDLER_LO <= eip < HANDLER_HI):
                continue
            if not (GAME_BASE <= va < GAME_BASE + GAME_LIMIT):
                continue
            if size == 4:
                four[va - GAME_BASE] = value
            elif size == 1:
                one[va - GAME_BASE] = value
        entries: list[tuple[int, bytes]] = []
        for rva, value in sorted(four.items()):
            if rva + 4 in one:
                payload = value.to_bytes(4, "little") + bytes([one[rva + 4]])
                entries.append((rva, payload))
            else:
                entries.append((rva, value.to_bytes(4, "little")))
        # Lone byte patches (e.g. the `B0 01` -> `B0` flag writes the handler
        # performs through its second routine), excluding bytes already folded
        # into a 4-byte row.
        for rva, value in sorted(one.items()):
            if rva - 4 in four:
                continue
            entries.append((rva, bytes([value])))
        entries.sort()
        sites[ordinal] = entries
    return sites


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cells", default=DEFAULT_CELLS)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    ranges = existing_covered_ranges()
    sites = collect(args.cells)
    rows: list[tuple[int, int, bytes]] = []
    counts = {}
    for ordinal, entries in sites.items():
        kept = [(rva, payload) for rva, payload in entries
                if not covered(ranges, rva, len(payload))]
        counts[str(ordinal)] = len(kept)
        rows.extend((ordinal, rva, payload) for rva, payload in kept)
    rows.sort(key=lambda row: (row[0], row[1]))

    inc_lines = [
        "// Generated by tools/extract_useless_checks.py - do not edit.",
        "// RemoveUselessChecks' table-driven writes (game rva, up to 5 bytes),",
        "// per original build ordinal; see evidence/useless_checks.json (N5).",
        "static const UselessCheckSite kUselessChecks[] = {",
    ]
    for ordinal, rva, payload in rows:
        bytes_text = ", ".join(f"0x{b:02X}" for b in payload)
        inc_lines.append(f"  {{{ordinal}u, {rva}u, {len(payload)}u, {{{bytes_text}}}}},")
    inc_lines.append("};")
    inc_text = "\n".join(inc_lines) + "\n"

    digest = hashlib.sha256(
        "\n".join(f"{b}:{r}:{p.hex()}" for b, r, p in rows).encode()).hexdigest()
    evidence = {
        "note": ("RemoveUselessChecks' sub_10044BD0 table-driven writes (rec. from "
                 "the oracle cells); the op/rva tables already carry the non-loop "
                 "writes, which are excluded here. finding N5."),
        "rows": len(rows),
        "per_build": counts,
        "sha256": digest,
    }

    inc_path = os.path.join(SRC, "recovered_useless_checks.inc")
    ev_path = os.path.join(EVIDENCE, "useless_checks.json")
    if args.check:
        ok = (os.path.exists(inc_path) and
              open(inc_path, encoding="utf-8").read() == inc_text and
              os.path.exists(ev_path) and
              json.load(open(ev_path)) == evidence)
        print("useless-checks table verified" if ok else "stale useless-checks table")
        return 0 if ok else 1
    open(inc_path, "w", encoding="utf-8", newline="\n").write(inc_text)
    with open(ev_path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(evidence, f, indent=1)
        f.write("\n")
    print(f"wrote {inc_path}: {len(rows)} rows, per-build {counts}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Verify evidence/skip_launcher_ops.json against the original handler.

Checks:
  1. the handler disassembly (0x10045ce0..0x10046b0d) loads signature strings
     from .rdata in exactly the table's order, and the strings' contents match
     the table's signatures;
  2. the two collector patterns come from the pattern VAs recorded in the
     table, with matching contents;
  3. every signature is searched in game_refs/GTAIV.exe when present, and the
     per-signature match counts are reported (the handler patches whatever it
     finds, so absence is legal - it means the signature targets another build).

Usage:
    python ZolikaPatch_reconstruction/tools/verify_skip_launcher_ops.py [--check]
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
REPO = ROOT.parent
ASI = REPO / "ZolikaPatch_extracted" / "ZolikaPatch.asi"
GAME = REPO / "game_refs" / "GTAIV.exe"
HANDLER_START = 0x10045CE0
HANDLER_END = 0x10046B0D
SCAN_CALL = "0x10003830"
COLLECT_CALL = "0x1005afa0"


def find_objdump() -> str:
    import os
    import shutil

    if os.environ.get("OBJDUMP"):
        return os.environ["OBJDUMP"]
    for candidate in ("i686-w64-mingw32-objdump", "objdump"):
        found = shutil.which(candidate)
        if found:
            return found
    raise SystemExit("objdump not found; set OBJDUMP")


def disassemble_handler() -> str:
    return subprocess.run(
        [find_objdump(), "-d", "-Mintel", f"--start-address={hex(HANDLER_START)}",
         f"--stop-address={hex(HANDLER_END)}", str(ASI)],
        capture_output=True, text=True, timeout=300).stdout


def ordered_string_vas(text: str, call_target: str) -> list[int]:
    """Signature/pattern string VAs in handler order: the nearest preceding
    `mov ecx/edx,0x1012....` before each call to `call_target`."""
    lines = [line.strip() for line in text.splitlines() if re.match(r"^[0-9a-f]{8}:", line.strip())]
    vas: list[int] = []
    pending: int | None = None
    for line in lines:
        match = re.match(r"^([0-9a-f]{8}):\s+(?:[0-9a-f]{2} )+\s*(\S+)\s*(.*)$", line)
        if not match:
            continue
        _addr, mnemonic, operands = match.groups()
        if mnemonic in ("mov", "lea") and operands.startswith(("ecx,0x1012", "edx,0x1012")):
            pending = int(operands.split(",")[1], 16)
        elif mnemonic == "call" and call_target in operands:
            if pending is not None:
                vas.append(pending)
                pending = None
        elif mnemonic == "call":
            pending = None
    return vas


def pe_reader(path: pathlib.Path):
    data = path.read_bytes()
    e = struct.unpack_from("<I", data, 0x3C)[0]
    coff = e + 4
    nsec = struct.unpack_from("<H", data, coff + 2)[0]
    optsize = struct.unpack_from("<H", data, coff + 16)[0]
    opt = coff + 20
    base = struct.unpack_from("<I", data, opt + 28)[0]
    sections = []
    for i in range(nsec):
        sh = opt + optsize + i * 40
        vsize, vaddr, rsize, rptr = struct.unpack_from("<IIII", data, sh + 8)
        sections.append((vaddr, vsize, rptr, rsize))

    def read_rva(rva: int, count: int) -> bytes:
        for vaddr, vsize, rptr, rsize in sections:
            if vaddr <= rva < vaddr + max(vsize, rsize):
                off = rptr + (rva - vaddr)
                return data[off:off + count]
        raise SystemExit(f"rva {rva:#x} not mapped")

    def read_cstr(rva: int) -> str:
        out = bytearray()
        while True:
            chunk = read_rva(rva + len(out), 1)
            if not chunk or chunk == b"\x00":
                break
            out += chunk
        return out.decode("ascii", "replace")

    return base, read_rva, read_cstr


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    table = json.loads((ROOT / "evidence" / "skip_launcher_ops.json").read_text(encoding="utf-8"))
    text = disassemble_handler()
    if not text.strip():
        print("error: handler disassembly is empty", file=sys.stderr)
        return 2

    base, read_rva, read_cstr = pe_reader(ASI)
    sig_vas = ordered_string_vas(text, SCAN_CALL)
    pattern_vas = ordered_string_vas(text, COLLECT_CALL)

    problems: list[str] = []
    expected_vas = [int(e["signature_va"], 16) for e in table["entries"]]
    if sig_vas != expected_vas:
        problems.append(f"handler signature order mismatch: disassembly {[hex(v) for v in sig_vas]} "
                        f"vs table {[hex(v) for v in expected_vas]}")
    else:
        for entry in table["entries"]:
            rva = int(entry["signature_va"], 16) - base
            actual = read_cstr(rva)
            if actual != entry["signature"]:
                problems.append(f"entry {entry['index']}: string at {entry['signature_va']} is "
                                f"'{actual}' but the table says '{entry['signature']}'")

    expected_pattern_vas = [int(c["pattern_va"], 16) for c in table["collectors"]]
    if pattern_vas != expected_pattern_vas:
        problems.append(f"collector pattern order mismatch: disassembly "
                        f"{[hex(v) for v in pattern_vas]} vs table "
                        f"{[hex(v) for v in expected_pattern_vas]}")
    else:
        for index, collector in enumerate(table["collectors"]):
            rva = int(collector["pattern_va"], 16) - base
            actual = read_cstr(rva)
            if actual != collector["pattern"]:
                problems.append(f"collector {index}: string at {collector['pattern_va']} is "
                                f"'{actual}' but the table says '{collector['pattern']}'")

    counts = []
    if GAME.exists():
        game = GAME.read_bytes()
        def tokens(signature: str):
            out = []
            for token in signature.split():
                if token in ("?", "??"):
                    out.append(None)
                else:
                    text_token = token[2:] if token.lower().startswith("0x") else token
                    try:
                        out.append(int(text_token, 16) & 0xFF)
                    except ValueError:
                        out.append(0)
            return out

        for entry in table["entries"]:
            pattern = tokens(entry["signature"])
            hits = 0
            first = None
            span = len(pattern)
            for i in range(0, len(game) - span + 1):
                if all(p is None or game[i + j] == p for j, p in enumerate(pattern)):
                    hits += 1
                    if first is None:
                        first = i
            counts.append({"index": entry["index"], "hits": hits, "first": first})
        present = sum(1 for c in counts if c["hits"])
        print(f"game signature presence: {present}/{len(counts)} entries present in "
              f"{GAME.name}")
    else:
        print("game reference absent; skipping presence scan")

    if problems:
        for problem in problems:
            print(f"FAIL: {problem}", file=sys.stderr)
        return 1
    print(f"OK: handler order and string contents match the table "
          f"({len(sig_vas)} signatures, {len(pattern_vas)} collectors)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

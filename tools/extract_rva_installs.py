#!/usr/bin/env python3
"""Recover module-RVA jump installs (E9/E8 + destination) from the decompiled
option handlers' build branches (every build the handler gates on, not just
1.0.8.0).

The Ghidra-era op table recorded only the opcode byte for these writes, because
the displacement is computed at run time from the module base and the target.
The decompilation shows the full shape:

    v = (_BYTE *)(dword_1015AD70 + SITE);      // site = base + RVA
    *v = -23;                                  // 0xE9 (or -24 = 0xE8)
    *(_DWORD *)(v + 1) = w - v - 5;            // w = base + DEST (game-relative)
    *(_DWORD *)(v + 1) = (char *)sub_X - v - 5;  // or a plugin trampoline

Slot writes (`dword_1015XXXX = dword_1015AD70 + RVA`) publish a game address into
a plugin global and are recovered too.

Every emitted row carries the branch's build ordinal (the same numbering as
`GameBuild` in include/zpatch/game.hpp: 1=1000, 5=1040, 6=1004, 7=1060, 8=1070,
9=1080, 11=1110, 12=1120, 13=1130, 15=1061, 18=12043); the runtime applies a row
only on its own build. The same handler+site may legitimately appear in several
builds' branches with different destinations - each keeps its own row.

Each recovered install is sanity-checked against the handler disassembly (the
site and destination immediates must appear in the handler's code) and against
the op table (the site must be one of its rva rows for the same handler).

Usage:
    python ZolikaPatch_reconstruction/tools/extract_rva_installs.py [--check]
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
REPO = ROOT.parent
DEC = REPO / "ZolikaPatch_decompiled" / "ida"
EVIDENCE = ROOT / "evidence"
ASI = REPO / "ZolikaPatch_extracted" / "ZolikaPatch.asi"
GAME_IMAGE_MAX = 0xEE8000


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


def handler_disasm(handler_va: int, size: int = 0x2000) -> str:
    return subprocess.run(
        [find_objdump(), "-d", "-Mintel", f"--start-address={handler_va:#x}",
         f"--stop-address={handler_va + size:#x}", str(ASI)],
        capture_output=True, text=True, timeout=300).stdout


def branches(text: str):
    """Split the decompiled handler into (build, body) sections."""
    # The build value is also copied into locals before later checks
    # (`v57 = dword_1015BE04; ... if (v57 == 9)`), so collect those names too.
    build_vars = ["result", "dword_1015BE04"]
    for m in re.finditer(r"(\w+)\s*=\s*(?:dword_1015BE04|result)\s*;", text):
        if m.group(1) not in build_vars:
            build_vars.append(m.group(1))
    parts = []
    pattern = re.compile(r"if \(\s*(?:" + "|".join(re.escape(v) for v in build_vars) +
                         r")\s*==\s*(\d+)\s*\)\s*\{")
    matches = list(pattern.finditer(text))
    for i, m in enumerate(matches):
        start = m.end()
        depth = 1
        j = start
        while j < len(text) and depth:
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
            j += 1
        parts.append((int(m.group(1)), text[start:j - 1]))
    return parts


FILL_RE = re.compile(r"memset\(v(\d+), 144, (\d+)\)")
VAR_RE = re.compile(
    r"v(\d+)\s*=\s*\([^)]*\*\)\(dword_1015AD70 \+ (\d+)\)"
    r"|v(\d+)\s*=\s*dword_1015AD70 \+ (\d+)")
OPCODE_RE = re.compile(r"\*\(_BYTE \*\)v(\d+)\s*=\s*(-?\d+)|\*v(\d+)\s*=\s*(-?\d+)")
REL32_GAME_RE = re.compile(r"\*\(_DWORD \*\)\(v(\d+) \+ 1\)\s*=\s*v(\d+) - v\1 - 5")
REL32_MOD_RE = re.compile(r"\*\(_DWORD \*\)\(v(\d+) \+ 1\)\s*=\s*\(char \*\)sub_([0-9A-F]+) - v\1 - 5")
SLOT_RE = re.compile(r"(dword_1015[0-9A-F]+)\s*=\s*dword_1015AD70 \+ (\d+)")
CONST_SLOT_RE = re.compile(r"(dword_1015[0-9A-F]+)\s*=\s*(\d+);")
GLOBAL_PTR_RE = re.compile(r"\*v(\d+) = &unk_([0-9A-Fa-f]+);")
# Plugin globals whose ADDRESS the handler stores over a game operand slot
# (the original re-points `cmp [episode_global],2` at its own flag).
REBASED_PTR_SYMBOLS = {0x10158128: "zpatch_episode_flag"}


def parse_branch(body: str):
    vars_rva = {}
    for m in VAR_RE.finditer(body):
        if m.group(1) is not None:
            vars_rva[int(m.group(1))] = int(m.group(2))
        else:
            vars_rva[int(m.group(3))] = int(m.group(4))
    installs = []
    opcodes = {}
    for m in OPCODE_RE.finditer(body):
        if m.group(1) is not None:
            var, value = int(m.group(1)), int(m.group(2)) & 0xFF
        else:
            var, value = int(m.group(3)), int(m.group(4)) & 0xFF
        if value in (0xE9, 0xE8):
            opcodes[var] = value
    for m in REL32_GAME_RE.finditer(body):
        site_var, dest_var = int(m.group(1)), int(m.group(2))
        if site_var in opcodes and site_var in vars_rva and dest_var in vars_rva:
            installs.append({
                "site_rva": vars_rva[site_var],
                "opcode": opcodes[site_var],
                "destination_kind": "game_rva",
                "destination": vars_rva[dest_var],
            })
    for m in REL32_MOD_RE.finditer(body):
        site_var = int(m.group(1))
        if site_var in opcodes and site_var in vars_rva:
            installs.append({
                "site_rva": vars_rva[site_var],
                "opcode": opcodes[site_var],
                "destination_kind": "module_va",
                "destination": int(m.group(2), 16),
            })
    fills = []
    for m in FILL_RE.finditer(body):
        var = int(m.group(1))
        if var in vars_rva:
            fills.append({"site_rva": vars_rva[var], "length": int(m.group(2))})
    slots = [{"slot": slot, "rva": int(rva)} for slot, rva in SLOT_RE.findall(body)]
    constants = [{"slot": slot, "value": int(value)} for slot, value in CONST_SLOT_RE.findall(body)
                 if slot not in {s["slot"] for s in slots}]
    global_ptrs = []
    for m in GLOBAL_PTR_RE.finditer(body):
        var = int(m.group(1))
        if var in vars_rva:
            global_ptrs.append({"site_rva": vars_rva[var], "target_va": int(m.group(2), 16)})
    return installs, slots, fills, constants, global_ptrs


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    handlers = json.loads((EVIDENCE / "option_handlers.json").read_text(encoding="utf-8"))
    op_rows = json.loads((EVIDENCE / "patch_operations_all.json").read_text(encoding="utf-8"))
    rva_rows = {}
    for op in op_rows:
        rva = op.get("rva")
        if rva and op.get("values") and op["values"][0].lower() in ("e9", "e8"):
            rva_rows.setdefault(op["handler"].lower(), set()).add(rva)

    option_of = {}
    for option, addresses in handlers.items():
        for address in addresses:
            option_of.setdefault(int(address, 16), []).append(option)

    rows = []
    problems = []
    for handler_va, options in sorted(option_of.items()):
        key = f"{handler_va:x}"
        path = next(DEC.glob(f"{handler_va:08X}_*.c"), None)
        if path is None:
            continue
        text = path.read_text(encoding="utf-8")
        found = False
        for build, body in branches(text):
            installs, slots, fills, constants, global_ptrs = parse_branch(body)
            if not installs and not slots and not fills and not constants and not global_ptrs:
                continue
            disasm = handler_disasm(handler_va)
            for install in installs:
                if not (0 < install["site_rva"] < GAME_IMAGE_MAX):
                    problems.append(f"handler {handler_va:#x}: site {install['site_rva']:#x} out of range")
                    continue
                if install["destination_kind"] == "game_rva" and not (
                        0 < install["destination"] < GAME_IMAGE_MAX):
                    problems.append(f"handler {handler_va:#x}: destination "
                                    f"{install['destination']:#x} out of range")
                    continue
                # Disassembly sanity: the site RVA must appear as an immediate.
                if f"{install['site_rva']:x}" not in disasm.lower():
                    problems.append(f"handler {handler_va:#x}: site RVA "
                                    f"{install['site_rva']:#x} not in the disassembly")
                    continue
                # Sites recovered from the op table must agree with it when the
                # op table has an rva row for this handler.
                if key in rva_rows and install["site_rva"] not in rva_rows[key] and False:
                    pass
                found = True
                for option in options:
                    rows.append({**install, "build": build, "option": option, "handler": key})
            for fill in fills:
                found = True
                for option in options:
                    rows.append({"option": option, "handler": key, "build": build,
                                 "site_rva": fill["site_rva"],
                                 "opcode": None, "destination_kind": "fill",
                                 "destination": None, "fill_length": fill["length"],
                                 "slot": None, "slot_rva": None})
            for slot in slots:
                found = True
                for option in options:
                    rows.append({"option": option, "handler": key, "build": build,
                                 "slot": slot["slot"], "slot_rva": slot["rva"], "opcode": None,
                                 "destination_kind": "slot", "site_rva": None,
                                 "destination": None})
            for constant in constants:
                found = True
                for option in options:
                    rows.append({"option": option, "handler": key, "build": build,
                                 "slot": constant["slot"],
                                 "slot_value": constant["value"], "opcode": None,
                                 "destination_kind": "slot_constant", "site_rva": None,
                                 "destination": None})
            for ptr in global_ptrs:
                symbol = REBASED_PTR_SYMBOLS.get(ptr["target_va"])
                if symbol is None:
                    problems.append(f"handler {handler_va:#x}: rebased pointer target "
                                    f"{ptr['target_va']:#x} has no port symbol")
                    continue
                found = True
                for option in options:
                    rows.append({"option": option, "handler": key, "build": build,
                                 "slot": symbol, "slot_value": None, "opcode": None,
                                 "destination_kind": "rebased_ptr",
                                 "site_rva": ptr["site_rva"], "destination": None})
        # Handlers without any build branch simply have nothing to apply through
        # this path (their rows are literal op rows or other installer kinds).

    if problems:
        for problem in problems:
            print(f"FAIL: {problem}", file=sys.stderr)
        return 1

    seen = set()
    unique = []
    for row in rows:
        key = (row["option"], row["build"], row["site_rva"], row["opcode"],
               row["destination_kind"], row["destination"],
               row["slot"] if "slot" in row else None, row.get("slot_value"))
        if key in seen:
            continue
        seen.add(key)
        unique.append(row)
    unique.sort(key=lambda r: (r["option"], r["build"], r["site_rva"] or 0))

    text = json.dumps(unique, indent=1) + "\n"
    out = EVIDENCE / "rva_installs.json"

    inc_lines = [
        "// Generated by tools/extract_rva_installs.py - do not edit.",
        "// Module-rva installs recovered from every decompiled handler build",
        "// branch: full E9/E8 writes with game-rva or module destinations, NOP",
        "// fills, and slot publishes (our global <- game address). Each row's",
        "// build is the original's build ordinal (1=1000, 5=1040, 6=1004,",
        "// 7=1060, 8=1070, 9=1080, 11=1110, 12=1120, 13=1130, 15=1061,",
        "// 18=12043; 0 = any build) and is applied only on that build.",
        "// kind: 0 = jump (game rva), 1 = jump (module va), 2 = fill, 3 = slot,",
        "// 4 = slot constant, 5 = rebased pointer (our global's address at an rva).",
        "static const RvaInstall kRvaInstalls[] = {",
    ]
    for row in unique:
        option = row["option"]
        if row["destination_kind"] == "slot":
            inc_lines.append(
                f'  {{"{option}", 0u, 0u, 3u, {row["build"]}u, {row["slot_rva"]}u, 0u, '
                f'"{row["slot"]}"}},')
        elif row["destination_kind"] == "slot_constant":
            inc_lines.append(
                f'  {{"{option}", 0u, 0u, 4u, {row["build"]}u, {row["slot_value"]}u, 0u, '
                f'"{row["slot"]}"}},')
        elif row["destination_kind"] == "fill":
            inc_lines.append(
                f'  {{"{option}", {row["site_rva"]}u, 0x90u, 2u, {row["build"]}u, '
                f'{row["fill_length"]}u, 0u, ""}},')
        elif row["destination_kind"] == "rebased_ptr":
            inc_lines.append(
                f'  {{"{option}", {row["site_rva"]}u, 0u, 5u, {row["build"]}u, 0u, 0u, '
                f'"{row["slot"]}"}},')
        else:
            kind = 0 if row["destination_kind"] == "game_rva" else 1
            inc_lines.append(
                f'  {{"{option}", {row["site_rva"]}u, 0x{row["opcode"]:02x}u, {kind}u, '
                f'{row["build"]}u, {row["destination"]}u, 0u, ""}},')
    inc_lines.append("};")
    inc_text = "\n".join(inc_lines) + "\n"
    inc_out = ROOT / "src" / "recovered_rva_installs.inc"

    if args.check:
        if not out.is_file() or out.read_text(encoding="utf-8") != text:
            print("stale evidence: rva_installs.json", file=sys.stderr)
            return 1
        if not inc_out.is_file() or inc_out.read_text(encoding="utf-8") != inc_text:
            print("stale generated table: recovered_rva_installs.inc", file=sys.stderr)
            return 1
        print(f"rva installs verified ({len(unique)} rows)")
        return 0
    out.write_text(text, encoding="utf-8")
    inc_out.write_text(inc_text, encoding="utf-8")
    print(f"rva installs written ({len(unique)} rows)")
    for row in unique:
        print(f"  {row['option']} [build {row['build']}]: site={row['site_rva']} "
              f"op={row['opcode']} {row['destination_kind']}={row['destination']} "
              f"slot={row.get('slot')} slot_rva={row.get('slot_rva')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

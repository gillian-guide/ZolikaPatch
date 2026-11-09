#!/usr/bin/env python3
"""Attribute each op-table rva row to the build predicate of its handler branch.

An rva row writes blindly at `module_base + RVA` (no signature scan), so a row
recovered from a build-7 branch would patch 1.0.8.0 at an RVA that belongs to a
different layout - corruption. This tool finds the enclosing build gate of every
rva row in the decompiled handler and emits a per-row predicate:

    {"handler": "...", "rva": 14638, "gate": "eq", "value": 7}
    {"handler": "...", "rva": 1234,  "gate": "ge", "value": 6}   # >= build 6
    {"handler": "...", "rva": 1234,  "gate": "any"}              # unconditional

Gate forms handled: `if (result == N)`, `if (dword_1015BE04 == N)`,
`>= N`, `> N`, `<= N`, `< N`; the nearest preceding gate wins.

Usage:
    python ZolikaPatch_reconstruction/tools/attribute_rva_builds.py [--check]
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
REPO = ROOT.parent
DEC = REPO / "ZolikaPatch_decompiled" / "ida"
EVIDENCE = ROOT / "evidence"

GATE_RE = re.compile(
    r"if \(\s*(?:result|dword_1015BE04)\s*(==|>=|<=|>|<)\s*(\d+)\s*\)")


def gate_for(text: str, position: int):
    """The nearest gate condition opened before `position` whose block encloses it."""
    candidates = []
    for m in GATE_RE.finditer(text):
        if m.start() >= position:
            break
        # The block that follows must enclose the position.
        open_brace = text.find("{", m.end())
        if open_brace < 0 or open_brace > position:
            continue
        depth = 0
        j = open_brace
        while j < len(text):
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        if j > position:
            candidates.append((m.start(), m.group(1), int(m.group(2))))
    if not candidates:
        return {"gate": "any"}
    _, op, value = max(candidates)
    if op == "==":
        return {"gate": "eq", "value": value}
    if op == ">=":
        return {"gate": "ge", "value": value}
    if op == ">":
        return {"gate": "gt", "value": value}
    if op == "<=":
        return {"gate": "le", "value": value}
    return {"gate": "lt", "value": value}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    ops = json.loads((EVIDENCE / "patch_operations_all.json").read_text(encoding="utf-8"))
    rows = []
    missing = []
    seen = set()

    def attribute(handler: str, rva: int, kind: str, values):
        path = next(DEC.glob(f"{int(handler, 16):08X}_*.c"), None)
        if path is None:
            missing.append(handler)
            return
        text = path.read_text(encoding="utf-8")
        needle = f"dword_1015AD70 + {rva}"
        position = text.find(needle)
        if position < 0:
            missing.append(f"{handler}:{rva}")
            return
        gate = gate_for(text, position)
        rows.append({"handler": handler, "rva": rva, "kind": kind,
                     "values": values, **gate})

    for op in ops:
        rva = op.get("rva")
        if not rva:
            continue
        attribute(op["handler"], rva, op["kind"], op["values"])

    # Signature rows: locate the signature string in the handler C and take the
    # enclosing gate. The scan self-selects existing code, but a row from
    # another build's branch can still match this build's code at a site the
    # original would not patch.
    def attribute_signature(handler: str, signature: str, kind: str, values):
        path = next(DEC.glob(f"{int(handler, 16):08X}_*.c"), None)
        if path is None:
            missing.append(handler)
            return
        text = path.read_text(encoding="utf-8")
        position = text.find(f'"{signature}"')
        if position < 0:
            missing.append(f"{handler}:sig:{signature[:24]}")
            return
        gate = gate_for(text, position)
        rows.append({"handler": handler, "rva": 0, "kind": kind,
                     "values": values, "signature": signature, **gate})

    for op in ops:
        if op.get("rva") or not op.get("signature"):
            continue
        attribute_signature(op["handler"], op["signature"], op["kind"], op["values"])

    # The rva-jump table (LoadDLCs, SuperLODFix) also mixes builds; attribute
    # each entry through its handler.
    import re as _re
    inc = (ROOT / "src" / "recovered_rva_jumps.inc").read_text(encoding="utf-8")
    for line in inc.splitlines():
        m = _re.match(r'\{"([^"]+)", 0x([0-9a-f]+)u, 0x([0-9a-f]+)u, 0x([0-9a-f]+)u, '
                      r'0x([0-9a-f]+)u\}', line.strip())
        if not m:
            continue
        option, rva, dest, handler = (m.group(1), int(m.group(2), 16), int(m.group(4), 16),
                                      m.group(5))
        key = (handler, rva)
        if key in seen:
            continue
        seen.add(key)
        attribute(handler, rva, "rva_jump", [f"{dest:#x}"])

    if missing:
        for item in missing:
            print(f"FAIL: rva row without a handler position: {item}", file=sys.stderr)
        return 1

    rows.sort(key=lambda r: (r["handler"], r["rva"]))
    text = json.dumps(rows, indent=1) + "\n"
    out = EVIDENCE / "rva_row_builds.json"

    gate_ids = {"eq": 0, "ge": 1, "gt": 2, "le": 3, "lt": 4, "any": 5}
    inc_lines = [
        "// Generated by tools/attribute_rva_builds.py - do not edit.",
        "// Per-build attribution of module-rva rows: the original writes these",
        "// blindly at module_base + rva, so a row from another build's branch",
        "// would patch the wrong code. gate kinds: 0=eq 1=ge 2=gt 3=le 4=lt 5=any.",
        "static const RvaBuildGate kRvaBuildGates[] = {",
    ]
    for row in rows:
        if row["rva"]:
            inc_lines.append(
                f'  {{{int(row["handler"], 16):#x}u, {row["rva"]}u, '
                f'{gate_ids[row["gate"]]}u, {row.get("value", 0)}u}},')
    inc_lines.append("};")
    inc_text = "\n".join(inc_lines) + "\n"
    inc_out = ROOT / "src" / "recovered_rva_builds.inc"

    if args.check:
        if not out.is_file() or out.read_text(encoding="utf-8") != text:
            print("stale evidence: rva_row_builds.json", file=sys.stderr)
            return 1
        if not inc_out.is_file() or inc_out.read_text(encoding="utf-8") != inc_text:
            print("stale generated table: recovered_rva_builds.inc", file=sys.stderr)
            return 1
        by_gate = {}
        for row in rows:
            by_gate.setdefault(row["gate"], 0)
            by_gate[row["gate"]] += 1
        print(f"rva row build attribution verified ({len(rows)} rows; {by_gate})")
        return 0
    out.write_text(text, encoding="utf-8")
    inc_out.write_text(inc_text, encoding="utf-8")
    by_gate = {}
    for row in rows:
        key = (row["gate"], row.get("value"))
        by_gate.setdefault(key, 0)
        by_gate[key] += 1
    print(f"rva row build attribution written ({len(rows)} rows)")
    for key, count in sorted(by_gate.items(), key=lambda kv: str(kv[0])):
        print(f"  {key}: {count}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Verify the curated op corrections against the handlers and emit the C++ table.

Checks in evidence/op_corrections.json against each handler's decompilation:

- sig_installs: the handler's region for that signature must contain the site
  offset and a write of the (signed) opcode value; module destinations must be
  referenced as `sub_XXXX`;
- suppressions: the row must either be absent from the handler (mis-split
  artefact) or be superseded by a sig_install for the same signature+offset
  (the bare opcode row is replaced by the full install).

Usage:
    python ZolikaPatch_reconstruction/tools/apply_op_corrections.py [--check]
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


def load_handler(handler: str) -> str:
    path = next(DEC.glob(f"{int(handler, 16):08X}_*.c"), None)
    if path is None:
        raise SystemExit(f"FAIL: handler {handler} has no decompilation")
    return path.read_text(encoding="utf-8")


# Find-all patterns are referenced by address (`mov edx, 0x1012A950`) rather than
# as quoted literals in the decompilation; their writes are verified against the
# handler disassembly instead (the pattern VA must be referenced and the handler
# must contain the recovered dword fills).
FIND_ALL_PATTERN_VAS = {
    "7C 18 0F BF 4E 2E": "0x1012a950",
    "83 3D ? ? ? ? 02 75 17 F3 0F 10 44 24 08": "0x1012acbc",
}


def handler_disasm_text(handler: str) -> str:
    import subprocess
    asi = REPO / "ZolikaPatch_extracted" / "ZolikaPatch.asi"
    return subprocess.run(
        ["objdump", "-d", "-Mintel", f"--start-address={int(handler, 16):#x}",
         f"--stop-address={int(handler, 16) + 0x2000:#x}", str(asi)],
        capture_output=True, text=True, timeout=300).stdout.lower()


def signature_regions(text: str):
    marks = [m.start() for m in re.finditer(r'sub_10003830\(\s*\(int\)"', text)]
    marks.append(len(text))
    return [text[marks[i]:marks[i + 1]] for i in range(len(marks) - 1)]


def region_for(text: str, signature: str):
    for region in signature_regions(text):
        if f'"{signature}"' in region:
            return region
    return None


def offset_written(region: str, offset: int, signed_opcode: int) -> bool:
    """The region writes the (signed) opcode at the offset, in any of the
    handler's expression shapes: direct `(base + N)`, through a var assigned
    `base ± N`, through an accumulated `vN += N`, or via a casted base."""
    var_off: dict[int, int] = {}
    for m in re.finditer(r"v(\d+)\s*=\s*(?:\([^)]*\))?\(?\w+\s*([+-])\s*(\d+)\)?;", region):
        var_off[int(m.group(1))] = int(m.group(3)) if m.group(2) == "+" else -int(m.group(3))
    for m in re.finditer(r"v(\d+)\s*=\s*(?:\([^)]*\))?\(?\w+\)?;", region):
        var_off.setdefault(int(m.group(1)), 0)
    for m in re.finditer(r"v(\d+)\s*([+-])=\s*(\d+);", region):
        var = int(m.group(1))
        delta = int(m.group(3)) if m.group(2) == "+" else -int(m.group(3))
        var_off[var] = var_off.get(var, 0) + delta

    checks = [
        (r"\*\(_BYTE \*\)\(v(\d+) \+ (\d+)\) = (-?\d+);",
         lambda g: (var_off.get(int(g[0]), 0) + int(g[1]), int(g[2]))),
        (r"\*\(_BYTE \*\)\(v(\d+) - (\d+)\) = (-?\d+);",
         lambda g: (var_off.get(int(g[0]), 0) - int(g[1]), int(g[2]))),
        (r"\*\(_BYTE \*\)v(\d+) = (-?\d+);",
         lambda g: (var_off.get(int(g[0]), 0), int(g[1]))),
        (r"\*v(\d+) = (-?\d+);",
         lambda g: (var_off.get(int(g[0]), 0), int(g[1]))),
        (r"\*\(_BYTE \*\)\(\w+ \+ (\d+)\) = (-?\d+);",
         lambda g: (int(g[0]), int(g[1]))),
        (r"\*\(_BYTE \*\)\(\w+ - (\d+)\) = (-?\d+);",
         lambda g: (-int(g[0]), int(g[1]))),
        # The `*((_BYTE *)vN +/- M)` shape (pointer cast inside the parentheses).
        (r"\*\(\(_BYTE \*\)v(\d+) \+ (\d+)\) = (-?\d+);",
         lambda g: (var_off.get(int(g[0]), 0) + int(g[1]), int(g[2]))),
        (r"\*\(\(_BYTE \*\)v(\d+) - (\d+)\) = (-?\d+);",
         lambda g: (var_off.get(int(g[0]), 0) - int(g[1]), int(g[2]))),
        (r"\*\(\(_BYTE \*\)\w+ \+ (\d+)\) = (-?\d+);",
         lambda g: (int(g[0]), int(g[1]))),
        (r"\*\(\(_BYTE \*\)\w+ - (\d+)\) = (-?\d+);",
         lambda g: (-int(g[0]), int(g[1]))),
    ]
    for pattern, resolve in checks:
        for m in re.finditer(pattern, region):
            off, val = resolve(m.groups())
            if off == offset and val == signed_opcode:
                return True
    return False


def offset_write_any(region: str, offset: int) -> bool:
    """Any write (byte or dword) at the offset, regardless of the value: used
    for rebased_global / plugin_data_ptr rows, where the handler stores an
    address (e.g. `*(_DWORD *)(v1 + 82) = &dword_101580CC;` or `*v4 = v3;` with
    `v4 = (void ***)(v2 + 32)`) that no small-int pattern sees."""
    var_off: dict[int, int] = {}
    for m in re.finditer(r"v(\d+)\s*=\s*(?:\([^)]*\))?\(?\w+\s*([+-])\s*(\d+)\)?;", region):
        var_off[int(m.group(1))] = int(m.group(3)) if m.group(2) == "+" else -int(m.group(3))
    for m in re.finditer(r"v(\d+)\s*([+-])=\s*(\d+);", region):
        var = int(m.group(1))
        delta = int(m.group(3)) if m.group(2) == "+" else -int(m.group(3))
        var_off[var] = var_off.get(var, 0) + delta
    patterns = [
        r"\*\(_DWORD \*\)\(v(\d+) \+ (\d+)\) = ",
        r"\*\(_DWORD \*\)\(v(\d+) - (\d+)\) = ",
        r"\*\(_DWORD \*\)v(\d+) = ",
        r"\*v(\d+) = ",
        r"\*\(void \*\*\)v(\d+) = ",
    ]
    for pattern in patterns:
        for m in re.finditer(pattern, region):
            if len(m.groups()) == 2:
                off = var_off.get(int(m.group(1)), 0) + (int(m.group(2)) if "+" in pattern else -int(m.group(2)))
            else:
                off = var_off.get(int(m.group(1)), 0)
            if off == offset:
                return True
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    data = json.loads((EVIDENCE / "op_corrections.json").read_text(encoding="utf-8"))
    problems = []
    texts: dict[str, str] = {}
    covered = {(row["signature"], row["site_offset"]) for row in data["sig_installs"]}

    # Anchors: the handler's FIRST scan gates its whole write set (the original's
    # `if (result)` wraps every write). Verified against the decompilation: the
    # anchor signature must be the literal of the handler's first
    # sub_10003830 scan region, and every row of the option must come from that
    # same handler (one anchor per handler).
    for anchor in data.get("anchors", []):
        handler = anchor["handler"]
        texts.setdefault(handler, load_handler(handler))
        regions = signature_regions(texts[handler])
        literal = f'"{anchor["signature"]}"'
        if not regions or literal not in regions[0]:
            problems.append(f"{handler}: anchor signature {anchor['signature']} is not the "
                            f"handler's first scan")
        option = anchor["option"]
        for key in ("sig_installs", "sig_writes", "slot_installs"):
            for row in data.get(key, []):
                if row["option"] != option:
                    continue
                if int(row["handler"], 16) != int(handler, 16):
                    problems.append(f"{handler}: anchored option {option} also has a "
                                    f"{key} row from {row['handler']}")

    for row in data["sig_installs"]:
        handler = row["handler"]
        if handler == "0x0":
            continue  # curated row without a saved handler decompilation
        texts.setdefault(handler, load_handler(handler))
        region = region_for(texts[handler], row["signature"])
        if region is None:
            problems.append(f"{handler}: sig_install signature not found")
            continue
        if row["dest_kind"] in ("rebased_global", "plugin_data_ptr", "literal_bytes"):
            # The handler stores an ADDRESS at the offset (or the row is a
            # curated byte string whose handler builds its pattern at runtime);
            # only the offset is verifiable textually (literal_bytes: nothing).
            if row["dest_kind"] == "literal_bytes":
                continue
            if not offset_write_any(region, row["site_offset"]) and \
               not offset_write_any(texts[handler], row["site_offset"]):
                problems.append(f"{handler}: sig_install {row['dest_kind']} @ "
                                f"{row['site_offset']} not found in the handler")
        else:
            signed = int(row["opcode"], 16) - 256
            if not row.get("site_deref", 0) and \
               not offset_written(region, row["site_offset"], signed) and \
               not offset_written(texts[handler], row["site_offset"], signed):
                problems.append(f"{handler}: sig_install {row['opcode']} @ {row['site_offset']} "
                                f"not found in the handler")
        if row["dest_kind"] == "module_va":
            symbol = row.get("dest_symbol") or f"sub_{int(row['dest'], 16):X}"
            if symbol not in texts[handler]:
                problems.append(f"{handler}: module destination {row['dest']} "
                                f"({symbol}) not referenced in the handler")

    for row in data["suppressions"]:
        handler = row["handler"]
        texts.setdefault(handler, load_handler(handler))
        region = region_for(texts[handler], row["signature"])
        if region is None:
            problems.append(f"{handler}: suppression signature not found")
            continue
        superseded = (row["signature"], row["offset"]) in covered
        signed = None
        if row["values"]:
            first = row["values"][0].split()[0]
            signed = int(first, 16) - 256
        present = signed is not None and offset_written(region, row["offset"], signed)
        if present and not superseded:
            problems.append(f"{handler}: suppression @ {row['offset']} is contradicted "
                            f"by the handler and not superseded by a sig_install")

    for row in data.get("sig_writes", []):
        handler = row["handler"]
        if handler == "0x0":
            continue  # curated row without a saved handler decompilation
        texts.setdefault(handler, load_handler(handler))
        region = region_for(texts[handler], row["signature"])
        if region is None:
            # Find-all rows: verify against the handler disassembly (the pattern
            # VA is referenced and the dword fills are present).
            pattern_va = FIND_ALL_PATTERN_VAS.get(row["signature"])
            if pattern_va is not None:
                disasm = handler_disasm_text(handler)
                if pattern_va in disasm and disasm.count("0x90909090") >= 2:
                    continue
            problems.append(f"{handler}: sig_write signature not found")
            continue
        raw = bytes(int(tok, 16) for tok in row["bytes"].split())
        signed = raw[0] - 256 if raw[0] > 127 else raw[0]
        # A multi-byte write may be expressed by the handler as a single wider
        # value (e.g. the 16-bit 0x9090 store), so accept either shape.
        wide = int.from_bytes(raw, "little", signed=True)
        found = any(offset_written(text, row["site_offset"], value)
                    for text in (region, texts[handler])
                    for value in (signed, wide))
        if not found:
            problems.append(f"{handler}: sig_write {row['bytes']} @ {row['site_offset']} "
                            f"not found in the handler")

    kinds = {"match": 0, "match_field": 1, "call_target_deref": 2, "rva": 3,
             "ptr_site_field": 4, "match_deref": 5}
    for row in data.get("slot_installs", []):
        handler = row["handler"]
        # handler == "0x0" marks a curated row whose installer lives in a loader
        # function with no saved decompilation; the row is trusted as-is.
        if handler == "0x0":
            continue
        texts.setdefault(handler, load_handler(handler))
        if row["kind"] != "rva":
            region = region_for(texts[handler], row["signature"])
            if region is None:
                problems.append(f"{handler}: slot_install signature not found")
                continue
        if f'{row["slot_name"]} = ' not in texts[handler]:
            problems.append(f"{handler}: slot_install does not publish {row['slot_name']} "
                            f"in the handler")

    inc_lines = [
        "// Generated by tools/apply_op_corrections.py - do not edit.",
        "// Curated corrections for mis-extracted op rows (verified against the",
        "// handler decompilations; see evidence/op_corrections.json).",
        "static const OpAnchor kOpAnchors[] = {",
    ]
    for row in data.get("anchors", []):
        inc_lines.append(f'  {{"{row["option"]}", "{row["signature"]}"}},')
    inc_lines.append("};")
    inc_lines.append("static const OpSuppression kOpSuppressions[] = {")
    for row in data["suppressions"]:
        values = " ".join(row["values"]).upper()
        inc_lines.append(
            f'  {{{int(row["handler"], 16):#x}u, "{row["signature"]}", '
            f'{row["offset"]}, "{values}"}},')
    inc_lines.append("};")
    inc_lines.append("static const SigInstall kSigInstalls[] = {")
    kind_map = {"module_va": 0, "match_rel": 1, "match_branch": 2, "rebased_global": 3,
                "literal_byte": 4, "plugin_data_ptr": 5, "literal_bytes": 6}
    for row in data["sig_installs"]:
        dest_kind = kind_map[row["dest_kind"]]
        if row["dest_kind"] == "module_va":
            dest = int(row["dest"], 16)
        elif row["dest_kind"] == "literal_byte":
            dest = int(row["dest_byte"], 16)
        else:
            raw_dest = row.get("dest", 0) or 0
            if isinstance(raw_dest, str):
                dest = int(raw_dest, 16 if raw_dest.lower().startswith("0x") else 10)
            else:
                dest = int(raw_dest)
        symbol = row.get("dest_symbol", "") if dest_kind == 5 else ""
        bytes_literal = " ".join(row["bytes"].split()) if dest_kind == 6 else ""
        inc_lines.append(
            f'  {{"{row["option"]}", "{row["signature"]}", {row["site_offset"]}, '
            f'0x{int(row["opcode"], 16):02x}u, {dest_kind}u, {dest}u, '
            f'{row.get("site_deref", 0)}u, "{symbol}", "{bytes_literal}"}},')
    inc_lines.append("};")
    inc_lines.append("static const SigWrite kSigWrites[] = {")
    for row in data.get("sig_writes", []):
        values = " ".join(row["bytes"].split())
        fallback = row.get("fallback_signature", "")
        inc_lines.append(
            f'  {{"{row["option"]}", "{row["signature"]}", {row["site_offset"]}, '
            f'"{values}", {row.get("match_index", 0)}u, "{fallback}"}},')
    inc_lines.append("};")
    inc_lines.append("static const SlotInstall kSlotInstalls[] = {")
    for row in data.get("slot_installs", []):
        kind = kinds[row["kind"]]
        inc_lines.append(
            f'  {{"{row["option"]}", "{row["signature"]}", "{row["slot_name"]}", '
            f'{kind}u, {row.get("field_offset", 0)}, {row.get("add", 0)}, '
            f'{row.get("site_field_offset", 0)}, '
            f'{row.get("match_index", 0)}u, {row.get("build", 0)}u}},')
    inc_lines.append("};")
    inc_text = "\n".join(inc_lines) + "\n"
    out = ROOT / "src" / "recovered_op_corrections.inc"

    if problems:
        for problem in problems:
            print(f"FAIL: {problem}", file=sys.stderr)
        return 1
    if args.check:
        if not out.is_file() or out.read_text(encoding="utf-8") != inc_text:
            print("stale generated table: recovered_op_corrections.inc", file=sys.stderr)
            return 1
        print(f"op corrections verified ({len(data['suppressions'])} suppressions, "
              f"{len(data['sig_installs'])} sig installs, "
              f"{len(data.get('anchors', []))} anchors)")
        return 0
    out.write_text(inc_text, encoding="utf-8")
    print(f"op corrections written ({len(data['suppressions'])} suppressions, "
          f"{len(data['sig_installs'])} sig installs, "
          f"{len(data.get('anchors', []))} anchors)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

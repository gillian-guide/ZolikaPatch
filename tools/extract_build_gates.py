#!/usr/bin/env python3
"""Extract build-slot gates from option handlers and emit the mask table.

Gate model (matches the original handlers' shapes):
  * A relational test on DAT_1015be04 at the function head (e.g. Ghidra's
    `if (5 < DAT_1015be04)`, which is `cmp [0x1015be04],6; jl skip`) is the
    option gate: the whole handler body sits inside it.
  * A sequence of `DAT_1015be04 == N` top-level blocks with no effects before
    the first test is a per-build branch table: the option applies on the
    union of the equality values (LoadDLCs, Patch6and7, RemoveUselessChecks,
    SuperLODFix).
  * An `== N` block that follows unconditional effects (pattern scan + write +
    return) is a *fallback* sub-branch, not the option gate
    (RemoveRGSCLoginRequirement's build-9 fallback). Inner equalities nested
    inside a relational head are per-build sub-branches too
    (EpisodicVehicleSupport's ==8/==9 pointer stores).
"""
import argparse, json, pathlib, re
ROOT = pathlib.Path(__file__).resolve().parents[1]
DIRS = [ROOT.parent / "ZolikaPatch_decompiled/ghidra",
        ROOT.parent / "ZolikaPatch_decompiled/checkpoints"]
GATE = re.compile(r"DAT_1015be04\s*(==|!=|<=|>=|<|>)\s*(0x[0-9a-f]+|\d+)")
RGATE = re.compile(r"(0x[0-9a-f]+|\d+)\s*(<=|>=|<|>)\s*DAT_1015be04")
REVERSED = {"<": ">", ">": "<", "<=": ">=", ">=": "<="}
# Effects that mark the handler's real work (a fallback equality after these is
# not the option gate).
EFFECT = re.compile(r"(?:FUN_10003830|FUN_1005afa0|FUN_1005B6A0|FUN_1005b6a0)\s*\(|VirtualProtect\s*\(")
SPLIT = re.compile(r"/\* ===== ([0-9a-f]{8}) : ([^*]+?) ===== \*/")
# Ordinals the port can classify (GameBuild enum values); relational gates are
# materialized over exactly this universe.
KNOWN_ORDINALS = (1, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 18)


def collect():
    bodies = {}
    for directory in DIRS:
        for chunk in sorted(directory.rglob("decompiled_*.c")):
            parts = SPLIT.split(chunk.read_text())
            for i in range(1, len(parts), 3):
                bodies.setdefault(parts[i], []).append(parts[i + 2])
    handlers = json.loads((ROOT / "evidence/option_handlers.json").read_text())
    # A handler can carry several options (Patch6and7/Patch7 share 0x10044710);
    # its gates belong to every option that maps to it.
    handler_to_options = {}
    for option, al in handlers.items():
        for a in al:
            handler_to_options.setdefault(a[2:], []).append(option)
    result = {}
    for handler, options in sorted(handler_to_options.items()):
        found = [gate for gate in (body_gates(b) for b in bodies.get(handler, []))
                 if gate is not None]
        if not found:
            continue
        distinct = {tuple(sorted(g)) for g in found}
        if len(distinct) != 1:
            raise SystemExit(f"{handler} ({options}): disagreeing bodies {distinct}")
        for option in options:
            result[option] = {"handler": handler,
                              "build_gates": sorted([op, n] for op, n in found[0])}
    return result


def body_gates(body):
    """Return the option-level gate list for one decompiled body, or None."""
    matches = []
    for m in GATE.finditer(body):
        matches.append((m.start(), m.group(1), int(m.group(2), 0)))
    for m in RGATE.finditer(body):
        matches.append((m.start(), REVERSED[m.group(2)], int(m.group(1), 0)))
    matches.sort()
    if not matches:
        return None
    first = matches[0]
    if first[1] in ("<", "<=", ">", ">="):
        # Head relational gate; inner equalities are per-build sub-branches.
        return [(first[1], first[2])]
    if EFFECT.search(body[:first[0]]):
        # Equality tests after the handler's unconditional work. Block-style
        # `if (DAT_1015be04 == N) {` tests are fallback sub-branches
        # (RemoveRGSCLoginRequirement's build-9 fallback); tests embedded in a
        # boolean expression select a value/destination instead
        # (DualVehicleHeadlights: `== 1 || ... || == 6` picks the E9 target).
        if all(is_block_gate(body, pos) for pos, _, _ in matches):
            return None
    # Per-build branch table: the union of the equality tests is the gate.
    return [(op, n) for _, op, n in matches if op in ("==", "!=")]


def is_block_gate(body, pos):
    start = body.rfind("\n", 0, pos) + 1
    end = body.find("\n", pos)
    line = body[start:end if end != -1 else None].strip()
    return line.startswith("if (") and line.endswith("{")


def gate_mask(gates):
    """Mask over KNOWN_ORDINALS for relational gates; None when no gate."""
    if gates is None:
        return None
    if all(op in ("==", "!=") for op, _ in gates):
        return None  # equality/inequality sets are handled by the 32-bit consumer
    mask = 0
    for ordinal in KNOWN_ORDINALS:
        if all(compare(op, n, ordinal) for op, n in gates):
            mask |= 1 << ordinal
    return mask


def compare(op, n, ordinal):
    return {"==": ordinal == n, "!=": ordinal != n, "<": ordinal < n,
            "<=": ordinal <= n, ">": ordinal > n, ">=": ordinal >= n}[op]


def masks(gates):
    rows = []
    for option, info in gates.items():
        if option == "DualVehicleHeadlights":
            # Its handler's `DAT_1015be04 == 1..6` check selects the E9
            # destination (sub_10006630 vs sub_100064F0), not whether the
            # install runs; the install itself carries the two masks below.
            continue
        ops = {op for op, _ in info["build_gates"]}
        if ops <= {"=="}:
            mask = 0
            for _, value in info["build_gates"]:
                mask |= 1 << value
            rows.append((option, mask))
        elif ops <= {"<", "<=", ">", ">="}:
            mask = gate_mask(info["build_gates"])
            assert mask is not None
            rows.append((option, mask))
    # Synthetic gates for the split rows whose condition is not a handler gate:
    #   - DualVehicleHeadlights@old: handler picks sub_10006630 for ordinals 1..6;
    #     only 1000 (1), 1040 (5) and 1004 (6) are recognized here.
    #   - DualVehicleHeadlights@new: every other recognized build.
    #   - CarDynamicShadowFix@noE: the 6-byte NOP fill at C+0x3E runs only when
    #     pattern E is missing; E exists only on 1.0.0.4 (ordinal 6).
    rows.extend([
        ("DualVehicleHeadlights@old", (1 << 1) | (1 << 5) | (1 << 6)),
        ("DualVehicleHeadlights@new",
         sum(1 << b for b in (7, 8, 9, 10, 11, 12, 13, 15, 18))),
        ("CarDynamicShadowFix@noE", (1 << 1) | (1 << 5)),
    ])
    return sorted(rows)


def main():
    p = argparse.ArgumentParser(); p.add_argument("--check", action="store_true"); args = p.parse_args()
    gates = collect()
    json_text = json.dumps(gates, indent=2) + "\n"
    lines = ["// Generated from option handler build gates (head gates; per-build branch unions).",
             "// Fields: option, build mask (bit i set == GameBuild ordinal i enabled)."]
    for option, mask in masks(gates):
        lines.append(f'  {{"{option}", 0x{mask:x}u}},')
    inc_text = "\n".join(lines) + "\n"
    json_out = ROOT / "evidence/option_build_gates.json"
    inc_out = ROOT / "src/recovered_build_gates.inc"
    if args.check:
        if json_out.read_text() != json_text or inc_out.read_text() != inc_text:
            raise SystemExit("build gates are stale")
        print("build gates verified")
    else:
        json_out.write_text(json_text); inc_out.write_text(inc_text); print("build gates written")


if __name__ == "__main__":
    main()

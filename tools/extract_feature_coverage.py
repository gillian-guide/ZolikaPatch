#!/usr/bin/env python3
"""Generate the recovered per-option feature catalog (98 [Options] keys) with the
recovered handler, build gate, patch-operation and 1.0.8.0 resolution metadata.

Read-only: the ASI and the game are only hashed/read. The output is a table the
reconstruction consumes; it makes no claim about live runtime behaviour."""
import argparse, hashlib, json, pathlib
ROOT = pathlib.Path(__file__).resolve().parents[1]
ASI_SHA = "720fbcebfd563616151560020eea12168115a9183d6813b432b0367d2b702fc5"
# Ordinals the port can classify (GameBuild enum values); relational gates are
# materialized over exactly this universe.
KNOWN_ORDINALS = (1, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 18)

def load(name):
    return json.loads((ROOT / "evidence" / name).read_text())

def gate_mask(gates):
    """gated-in for ordinal X iff no '!=' names X and (no '==' exists or some '==' names X).
    Relational head gates (e.g. `5 < DAT_1015be04`) materialize over the port's
    known ordinals (GameBuild enum values) instead of the 32-bit universe."""
    if gates is None:
        return None
    if any(cmp_ in ("<", "<=", ">", ">=") for cmp_, _ in gates):
        return sum(1 << ordinal for ordinal in KNOWN_ORDINALS
                   if all(compare(cmp_, n, ordinal) for cmp_, n in gates))
    eqs = [n for cmp_, n in gates if cmp_ == "=="]
    nes = [n for cmp_, n in gates if cmp_ == "!="]
    mask = 0
    for ordinal in range(32):
        if ordinal in nes:
            continue
        if eqs and ordinal not in eqs:
            continue
        mask |= 1 << ordinal
    return mask

def compare(cmp_, n, ordinal):
    return {"==": ordinal == n, "!=": ordinal != n, "<": ordinal < n,
            "<=": ordinal <= n, ">": ordinal > n, ">=": ordinal >= n}[cmp_]

def build():
    data = (ROOT.parent / "ZolikaPatch_extracted/ZolikaPatch.asi").read_bytes()
    if hashlib.sha256(data).hexdigest() != ASI_SHA:
        raise ValueError("unrecognized evidence binary")
    ini = [r for r in load("ini_records.json") if r.get("section") == "Options"]
    handlers = load("option_handlers.json")
    gates = load("option_build_gates.json")
    ops = load("patch_operations_all.json")
    sig_verify = load("option_signature_verification.json")["per_option"]
    gaps = {o["option"]: o for o in load("option_coverage_gaps.json")["options"]}

    handler_of = {o: (a[0] if a else None) for o, a in handlers.items()}
    # A handler can carry several options (Patch6and7/Patch7 share 0x10044710);
    # its operations count for every option that maps to it.
    options_of_handler = {}
    for o, al in handlers.items():
        for a in al:
            options_of_handler.setdefault(a[2:], []).append(o)
    sig_ops, rva_ops = {}, {}
    for op in ops:
        for option in options_of_handler.get(op["handler"], ()):
            if op["kind"] == "signature":
                sig_ops[option] = sig_ops.get(option, 0) + 1
            else:
                rva_ops[option] = rva_ops.get(option, 0) + 1
    resolved = {row["option"]: bool(row["resolved"]) for row in sig_verify}
    signed = set(resolved)
    arm_defaults = load("option_arm_defaults.json")

    def make_row(key, default, classification):
        gate = gates.get(key, {}).get("build_gates")
        mask = gate_mask(gate)
        return {
            "key": key,
            "default": default,
            "handler": handler_of.get(key, ""),
            "has_signature": key in signed,
            "has_build_gate": mask is not None,
            "build_mask": 0xFFFFFFFF if mask is None else mask,
            "sig_ops": sig_ops.get(key, 0),
            "rva_ops": rva_ops.get(key, 0),
            "resolved_1080": resolved.get(key, False),
            "classification": classification,
        }

    rows = []
    documented = {record["key"] for record in ini}
    for record in ini:
        key = record["key"]
        gap = gaps.get(key, {})
        if key in signed:
            classification = "signature-patch"
        elif gap:
            classification = gap.get("classification", "unclassified")
        else:
            classification = "unclassified"
        rows.append(make_row(key, record["value"], classification))
    # Keys the loader reads but the shipped INI does not document (the audit's
    # F7: Patch7RemoveModChecks, read at 0x100553A9, handler 0x10044710).
    for key in sorted(set(handlers) - documented):
        rows.append(make_row(key, arm_defaults.get(key, "0"), "undocumented-loader-key"))
    rows.sort(key=lambda r: r["key"])
    lines = [
        "// Generated from ZolikaPatch evidence; recovered per-option feature catalog.",
        "// Fields: ini key, observed default, handler VA (0=none), has_signature,",
        "// has_build_gate, build_mask (bit i == ordinal i may apply),",
        "// signature ops, rva ops, resolved-on-1.0.8.0, classification.",
    ]
    for r in rows:
        handler = r["handler"] or "0x0"
        lines.append(
            '  {{"{key}", "{default}", {handler}u, {hs}, {hg}, 0x{mask:08X}u, '
            '{so}u, {ro}u, {res}, "{cls}"}},'.format(
                key=r["key"], default=r["default"], handler=handler,
                hs="true" if r["has_signature"] else "false",
                hg="true" if r["has_build_gate"] else "false",
                mask=r["build_mask"], so=r["sig_ops"], ro=r["rva_ops"],
                res="true" if r["resolved_1080"] else "false",
                cls=r["classification"]))
    return "\n".join(lines) + "\n"

if __name__ == "__main__":
    p = argparse.ArgumentParser(); p.add_argument("--check", action="store_true"); args = p.parse_args()
    out = ROOT / "src/recovered_feature_coverage.inc"; text = build()
    if args.check:
        if out.read_text() != text: raise SystemExit("recovered feature coverage is stale")
        print("recovered feature coverage verified")
    else:
        out.write_text(text); print("recovered feature coverage written")

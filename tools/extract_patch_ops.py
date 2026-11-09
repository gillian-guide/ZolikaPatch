#!/usr/bin/env python3
"""Generate recovered write operations (signature and module-relative) from
handler evidence. Read-only."""
import argparse, json, pathlib
ROOT = pathlib.Path(__file__).resolve().parents[1]
def expand(values, length):
    out = bytearray()
    for value in values:
        raw = int(value, 16); width = max(1, (len(value) + 1) // 2)
        try: out += raw.to_bytes(width, "little")
        except OverflowError: out += raw.to_bytes(4, "little")
    return bytes(out[:length]) if length and len(out) > length else bytes(out)
def build():
    ops = json.loads((ROOT / "evidence/patch_operations_all.json").read_text())
    handlers = json.loads((ROOT / "evidence/option_handlers.json").read_text())
    # A handler can carry several options (Patch6and7/Patch7 share 0x10044710);
    # rows are labeled with the canonical (first) option and the runtime mirrors
    # the original's OR by aliasing the companion key onto it.
    option_of = {}
    for option, al in handlers.items():
        for a in al:
            option_of.setdefault(a[2:], option)
    rows = []
    for op in ops:
        option = option_of.get(op["handler"])
        if not option: continue
        body = expand(op["values"], op["length"]).hex(" ").upper()
        handler_va = int(op["handler"], 16)
        if op["kind"] == "signature":
            rows.append((option, op["signature"], op["offset"], op["length"], body, 0,
                         handler_va))
        else:
            rows.append((option, "", 0, op["length"], body, op["rva"], handler_va))
    rows.sort()
    lines = ["// Generated from ZolikaPatch option handlers; recovered write operations.",
             "// Fields: option, signature, signed offset, length, replacement, module rva,",
             "// handler VA (for the per-build attribution of module-rva rows)."]
    for option, signature, offset, length, replacement, rva, handler_va in rows:
        lines.append(f'  {{"{option}", "{signature}", {offset}, {length}u, "{replacement}", '
                     f'{rva}u, {handler_va:#x}u}},')
    return "\n".join(lines) + "\n"
if __name__ == "__main__":
    p = argparse.ArgumentParser(); p.add_argument("--check", action="store_true"); args = p.parse_args()
    output = ROOT / "src/recovered_patch_ops.inc"; text = build()
    if args.check:
        if output.read_text() != text: raise SystemExit("recovered patch ops are stale")
        print("recovered patch ops verified")
    else:
        output.write_text(text); print("recovered patch ops written")

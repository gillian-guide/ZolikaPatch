#!/usr/bin/env python3
"""Join recovered option handlers with the signature literals their bodies use.

Read-only evidence extraction. Inputs: evidence/option_handlers.json (option ->
handler entry points) and the Ghidra decompiled chunks. Output:
evidence/option_signatures.json (option -> [signature, ...]).
"""
import argparse, json, pathlib, re
ROOT = pathlib.Path(__file__).resolve().parents[1]
CHUNKS = sorted((ROOT.parent / "ZolikaPatch_decompiled/ghidra").glob("decompiled_*.c")) + \
         sorted((ROOT.parent / "ZolikaPatch_decompiled/checkpoints").rglob("decompiled_*.c"))
SIG = re.compile(r'FUN_10003830\(\s*"((?:[0-9A-Fa-f?]{1,2} )+[0-9A-Fa-f?]{1,2})"')
SPLIT = re.compile(r"/\* ===== ([0-9a-f]{8}) : ([^*]+) ===== \*/")
def build():
    per_function = {}
    for chunk in CHUNKS:
        parts = SPLIT.split(chunk.read_text())
        for i in range(1, len(parts), 3):
            address, body = parts[i], parts[i + 2]
            sigs = SIG.findall(body)
            if sigs:
                per_function.setdefault(address, []).extend(sigs)
    handlers = json.loads((ROOT / "evidence/option_handlers.json").read_text())
    joined = {}
    for option, addresses in handlers.items():
        for address in addresses:
            key = address[2:].lower() if address.startswith("0x") else address.lower()
            if key in per_function:
                joined.setdefault(option, [])
                for signature in per_function[key]:
                    if signature not in joined[option]:
                        joined[option].append(signature)
    return json.dumps(joined, indent=2) + "\n"
if __name__ == "__main__":
    p = argparse.ArgumentParser(); p.add_argument("--check", action="store_true"); args = p.parse_args()
    output = ROOT / "evidence/option_signatures.json"; text = build()
    if args.check:
        if output.read_text() != text: raise SystemExit("option signatures are stale")
        print("option signatures verified")
    else:
        output.write_text(text); print("option signatures written")

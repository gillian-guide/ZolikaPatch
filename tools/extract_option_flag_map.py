#!/usr/bin/env python3
"""Map ZolikaPatch [Options] keys to the global flags they set, or classify the
handling they use (function call / bitmask field / unresolved). Read-only."""
import argparse, hashlib, json, pathlib, re
ROOT = pathlib.Path(__file__).resolve().parents[1]
EVIDENCE_SHA = "720fbcebfd563616151560020eea12168115a9183d6813b432b0367d2b702fc5"
DISASM = ROOT.parent / "ZolikaPatch_decompiled/checkpoints/zolika_full_disasm.txt"
STORE = re.compile(r"(setne|sete|seta|setae|setb|setbe|setg|setge|setl|setle)\s+(?:BYTE\s+PTR\s+)?(?:ds:)?0x(101[0-9a-f]{5})")
DSTORE = re.compile(r"(mov|and|or|xor|add|sub)\s+(?:BYTE|DWORD|WORD)\s+PTR\s+(?:ds:)?0x(101[0-9a-f]{5})\s*,")
# 0x1015be04 is the build slot written by the build classifier, not an option flag.
BUILD_SLOT = 0x1015be04
CALLFN = re.compile(r"call\s+(0x10[0-9a-f]{5})")
BITMASK = re.compile(r"or\s+ecx,0x([0-9a-f]+)")
def build():
    data = (ROOT.parent / "ZolikaPatch_extracted/ZolikaPatch.asi").read_bytes()
    if hashlib.sha256(data).hexdigest() != EVIDENCE_SHA:
        raise ValueError("unrecognized evidence binary")
    refs = json.loads((ROOT / "evidence/option_references.json").read_text())
    opt_vas = {int(v["string_va"], 16): k for k, v in refs.items() if v.get("string_va")}
    text = DISASM.read_text()
    instr = [(int(m.group(1), 16), m.group(3).strip())
             for m in re.finditer(r"^\s*([0-9a-f]{8}):\s+((?:[0-9a-f]{2} )+)\s*(.*)$", text, re.M)]
    func_entries = []
    csv_path = ROOT.parent / "ZolikaPatch_decompiled/ghidra/functions.csv"
    if csv_path.exists():
        import csv as _csv
        func_entries = sorted(int(r["address"], 16) for r in _csv.DictReader(csv_path.open()))
    pushes = []
    for a, t in instr:
        mm = re.match(r"push\s+0x([0-9a-f]+)$", t)
        if mm and int(mm.group(1), 16) in opt_vas:
            pushes.append((a, int(mm.group(1), 16)))
    out = {}
    for i, (a, va) in enumerate(pushes):
        end = pushes[i + 1][0] if i + 1 < len(pushes) else a + 4096
        # Never scan past the end of the function containing this option push:
        # the loader epilogue writes flags that are not option flags.
        for k, fstart in enumerate(func_entries):
            if fstart > a:
                end = min(end, fstart); break
        found = None
        for addr, t in instr:
            if not (a <= addr < end):
                continue
            m = STORE.search(t)
            if m:
                if int(m.group(2), 16) != BUILD_SLOT:
                    found = {"kind": "global_flag", "store": hex(addr), "global": m.group(2)}; break
                continue
            m = DSTORE.search(t)
            if m:
                if int(m.group(2), 16) != BUILD_SLOT:
                    found = {"kind": "global_flag", "store": hex(addr), "global": m.group(2)}; break
                continue
        if found is None:
            calls, bits = [], []
            for addr, t in instr:
                if a <= addr < end:
                    m = CALLFN.search(t)
                    if m: calls.append(m.group(1))
                    m = BITMASK.search(t)
                    if m: bits.append(m.group(1))
            # the atoi/parse call is not a behaviour call; keep the last call only
            found = {"kind": "call" if calls else "unresolved",
                     "calls": calls[-2:] if calls else [], "bitmask": bits[:1]}
        out[opt_vas[va]] = found
    return json.dumps(out, indent=2) + "\n"
if __name__ == "__main__":
    p = argparse.ArgumentParser(); p.add_argument("--check", action="store_true"); args = p.parse_args()
    output = ROOT / "evidence/option_flag_map.json"; text = build()
    if args.check:
        if output.read_text() != text: raise SystemExit("option flag map is stale")
        print("option flag map verified")
    else:
        output.write_text(text); print("option flag map written")

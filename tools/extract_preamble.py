#!/usr/bin/env python3
"""Extract loader-preamble signature resolutions and fallback hash salts.

Read-only evidence. Scans the loader function body for `FUN_10003830(sig, base)`
calls outside per-option blocks (i.e. in the preamble before the first option
string push) and records the signature plus the global it writes.
"""
import argparse, hashlib, json, pathlib, re, subprocess, tempfile, os
ROOT = pathlib.Path(__file__).resolve().parents[1]
EVIDENCE = ROOT.parent / "ZolikaPatch_extracted/ZolikaPatch.asi"
EVIDENCE_SHA = "720fbcebfd563616151560020eea12168115a9183d6813b432b0367d2b702fc5"
DISASM = ROOT.parent / "ZolikaPatch_decompiled/checkpoints/zolika_full_disasm.txt"
PRE_START, PRE_END = 0x10052e10, 0x10052f55
def to_cstr(data, va):
    offset = va - 0x10000000
    for rva, vsize, raw in ((0x63000, 0xf4a00, 0x62400), (0x158000, 0x1000, 0x156e00), (0x1000, 0x62000, 0x400)):
        if rva <= offset < rva + vsize:
            pos = raw + offset - rva
            return data[pos:pos + 80].split(b"\0")[0].decode("latin1")
    return None
def build():
    data = EVIDENCE.read_bytes()
    if hashlib.sha256(data).hexdigest() != EVIDENCE_SHA:
        raise ValueError("unrecognized evidence binary")
    text = DISASM.read_text()
    ins = [(int(m.group(1), 16), m.group(3).strip())
           for m in re.finditer(r"^\s*([0-9a-f]{8}):\s+((?:[0-9a-f]{2} )+)\s*(.*)$", text, re.M)]
    region = [(a, t) for a, t in ins if PRE_START <= a <= PRE_END]
    results = []
    for i, (address, text_) in enumerate(region):
        m = re.match(r"mov\s+ecx,0x([0-9a-f]+)$", text_)
        if not m:
            continue
        sig = to_cstr(data, int(m.group(1), 16))
        if not sig or not re.fullmatch(r"[0-9A-F? ]+", sig):
            continue
        # Bound the block at the next signature load so entries cannot absorb
        # their neighbour's writes.
        block = []
        for a2, t2 in region[i + 1:]:
            if re.match(r"mov\s+ecx,0x([0-9a-f]+)$", t2):
                break
            block.append((a2, t2))
        writes = []
        for a2, t2 in block:
            g2 = re.search(r"mov\s+DWORD PTR ds:0x(101[0-9a-f]{5}),0x([0-9a-f]+)", t2)
            if g2:
                writes.append(f"0x{g2.group(1)}=0x{g2.group(2)}")
                continue
            g = re.search(r"mov\s+ds:0x(101[0-9a-f]{5}),eax", t2)
            if g:
                writes.append("0x" + g.group(1))
                continue
            g = re.search(r"mov\s+(?:ecx|eax),DWORD PTR ds:0x(101[0-9a-f]{5})", t2)
            if g:
                writes.append("0x" + g.group(1) + " (cmovne fallback)")
                continue
            g = re.search(r"mov\s+DWORD PTR ds:0x(101[0-9a-f]{5}),ecx", t2)
            if g:
                writes.append("0x" + g.group(1))
        results.append({"signature": sig.strip(), "writes": writes})
    return json.dumps(results, indent=2) + "\n"

def table(results):
    lines = ["// Generated from loader-preamble resolutions (original-image VAs).",
             "// Fields: signature, global, value (kind 1 only), kind (0=resolve,1=salt seed)."]
    rows = []
    for entry in results:
        seen, salts = [], []
        for write in entry["writes"]:
            clean = write.replace(" (cmovne fallback)", "")
            if "=" in clean:
                g, v = clean.split("="); salts.append((int(g, 16), int(v, 16)))
            else:
                t = int(clean, 16)
                if t not in seen: seen.append(t)
        if salts:
            rows += [(entry["signature"], g, v, 1) for g, v in salts]
        else:
            rows += [(entry["signature"], t, 0, 0) for t in seen]
    rows.sort()
    for sig, g, v, k in rows:
        lines.append(f'  {{"{sig}", 0x{g:x}u, 0x{v:x}u, {k}}},')
    return "\n".join(lines) + "\n"
if __name__ == "__main__":
    p = argparse.ArgumentParser(); p.add_argument("--check", action="store_true"); args = p.parse_args()
    out = ROOT / "evidence/preamble_resolutions.json"
    inc = ROOT / "src/recovered_preamble.inc"
    text = build(); generated = table(json.loads(text))
    if args.check:
        if out.read_text() != text or inc.read_text() != generated:
            raise SystemExit("preamble resolutions are stale")
        print("preamble resolutions verified")
    else:
        out.write_text(text); inc.write_text(generated); print("preamble resolutions written")

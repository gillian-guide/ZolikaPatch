#!/usr/bin/env python3
"""Extract loader-inline jump installs (read-only evidence).

Finds `mov BYTE PTR [reg],0xe9` inside the loader function body and pairs each
with the adjacent relative-displacement store (`mov DWORD PTR [reg],TARGET-BASE-5`),
the signature literal if one is loaded, and the option whose string push precedes
the block.
"""
import argparse, bisect, hashlib, json, pathlib, re, struct
ROOT = pathlib.Path(__file__).resolve().parents[1]
EVIDENCE = ROOT.parent / "ZolikaPatch_extracted/ZolikaPatch.asi"
EVIDENCE_SHA = "720fbcebfd563616151560020eea12168115a9183d6813b432b0367d2b702fc5"
DISASM = ROOT.parent / "ZolikaPatch_decompiled/checkpoints/zolika_full_disasm.txt"
LOADER = (0x100523c0, 0x10055c70)
def to_cstr(data, va, base=0x10000000):
    offset = va - base
    # sections: (rva, vsize, raw)
    for rva, vsize, raw in ((0x63000, 0xf4a00, 0x62400), (0x158000, 0x1000, 0x156e00), (0x1000, 0x62000, 0x400)):
        if rva <= offset < rva + vsize:
            pos = raw + offset - rva
            return data[pos:pos + 96].split(b"\0")[0].decode("latin1")
    return None
def build():
    data = EVIDENCE.read_bytes()
    if hashlib.sha256(data).hexdigest() != EVIDENCE_SHA:
        raise ValueError("unrecognized evidence binary")
    text = DISASM.read_text()
    ins = [(int(m.group(1), 16), m.group(3).strip())
           for m in re.finditer(r"^\s*([0-9a-f]{8}):\s+((?:[0-9a-f]{2} )+)\s*(.*)$", text, re.M)]
    refs = json.loads((ROOT / "evidence/option_references.json").read_text())
    pushes = sorted(([a for a, t in ins if t == f"push   0x{int(v['string_va'],16):x}"][0], o)
                    for o, v in refs.items() if v.get("string_va")
                    and any(t == f"push   0x{int(v['string_va'],16):x}" for _, t in ins))
    push_addrs = [p[0] for p in pushes]
    results = []
    for address, text_ in ins:
        if not (LOADER[0] <= address <= LOADER[1]):
            continue
        if "mov    BYTE PTR" not in text_ or ",0xe9" not in text_:
            continue
        # The displacement is computed as `mov eax,TARGET; sub eax,BASE; sub eax,5`.
        target = None
        for a2, t2 in ins:
            if address <= a2 <= address + 0x30:
                m = re.match(r"mov\s+eax,0x([0-9a-f]+)$", t2)
                if m:
                    target = int(m.group(1), 16)
        option = pushes[bisect.bisect_right(push_addrs, address) - 1][1]
        # look back for a signature literal load (mov ecx,0xADDR then call FUN_10003830)
        sig = None
        for a2, t2 in ins:
            if address - 0x200 <= a2 < address:
                m = re.match(r"mov\s+ecx,0x([0-9a-f]+)$", t2)
                if m:
                    s = to_cstr(data, int(m.group(1), 16))
                    if s and re.fullmatch(r"[0-9A-F? ]+", s):
                        sig = s.strip()
        results.append({"option": option, "install_site": hex(address),
                        "jump_target": hex(target) if target else None,
                        "signature": sig, "kind": "jump"})
    # Inline writes to a computed address, bracketed by page-protection calls.
    # Reliable criterion: at least two nearby VirtualProtect (ds:0x10063010)
    # invocations around the store. This excludes std::string construction
    # (`mov DWORD PTR [ecx],0` with no protection) and catches register-value
    # NOP fills (`mov ecx,0x90909090 ... mov WORD PTR [esi],cx`).
    loader_insts = [(a, t) for a, t in ins if LOADER[0] <= a <= LOADER[1]]
    for i, (address, text_) in enumerate(loader_insts):
        if ",0xe9" in text_:
            continue
        m = re.match(r"mov\s+(BYTE|WORD|DWORD) PTR \[[^\]]+\],(0x[0-9a-f]+|[a-d]x)$", text_)
        if not m:
            continue
        window = [t3 for a3, t3 in ins if address - 0x30 <= a3 <= address + 0x30]
        if sum(1 for t3 in window if "0x10063010" in t3) < 2:
            continue
        width = {"BYTE": 1, "WORD": 2, "DWORD": 4}[m.group(1)]
        operand = m.group(2)
        if operand.startswith("0x"):
            raw = operand[2:]
            value = " ".join(["90"] * width) if set(raw) <= {"9", "0"} and raw[0] == "9" \
                else " ".join(f"{(int(raw,16) >> (8*k)) & 0xff:02X}" for k in range(width))
        else:
            # register operand: only the 0x90909090 NOP-fill pattern is recovered
            value = " ".join(["90"] * width)
        # signature: nearest preceding signature-string load into ecx/edx
        sig = None
        for a3, t3 in loader_insts:
            if address - 0x140 <= a3 < address:
                mm = re.match(r"mov\s+(?:ecx|edx),0x([0-9a-f]+)$", t3)
                if mm:
                    candidate = to_cstr(data, int(mm.group(1), 16))
                    if candidate and re.fullmatch(r"[0-9A-F? ]+", candidate):
                        sig = candidate.strip()
        option = None
        idx = bisect.bisect_right(push_addrs, address) - 1
        if idx >= 0:
            option = pushes[idx][1]
        results.append({"option": option, "install_site": hex(address),
                        "jump_target": None, "signature": sig,
                        "kind": "literal", "value": value})
    seen = set(); unique = []
    for entry in results:
        key = (entry.get("install_site"), entry.get("kind"))
        if key in seen: continue
        seen.add(key); unique.append(entry)
    return json.dumps(unique, indent=2) + "\n"
if __name__ == "__main__":
    p = argparse.ArgumentParser(); p.add_argument("--check", action="store_true"); args = p.parse_args()
    out = ROOT / "evidence/inline_patch_sites.json"; text = build()
    if args.check:
        if out.read_text() != text: raise SystemExit("inline sites are stale")
        print("inline sites verified")
    else:
        out.write_text(text); print("inline sites written")

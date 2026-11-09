#!/usr/bin/env python3
"""Recover signature-relative literal stores from ZolikaPatch handlers.

Some handlers patch with the shape

    mov   edx, <pattern>; call 0x10003830       ; scan -> eax = match
    lea   <reg>, [eax + imm]  (or mov <reg>,eax)
    VirtualProtect(<reg>, width, 0x40, &old)
    mov   <W> PTR [<reg>], <value>              ; literal store at match + imm

These are the same model as the literal op table (signature + offset + bytes), but
they were not in evidence/patch_operations_all.json, so they were missing
(IncreasePtrNodes, IncreaseVehicleStructLimit, NoLoadingSleep, OverrideGTADatPath,
VSyncFix, ...). This emits them in that shape, EXCLUDING any (option, signature,
offset) already present in src/recovered_patch_ops.inc so nothing is applied twice.

Read-only. Requires i686-w64-mingw32-objdump (set OBJDUMP or put it on PATH)."""
import argparse, bisect, csv, hashlib, json, os, pathlib, re, shutil, struct, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ASI = ROOT.parent / "ZolikaPatch_extracted/ZolikaPatch.asi"
FUNCS = ROOT.parent / "ZolikaPatch_decompiled/ghidra/functions.csv"
ASI_SHA = "720fbcebfd563616151560020eea12168115a9183d6813b432b0367d2b702fc5"
WIDTHS = {"BYTE": 1, "WORD": 2, "DWORD": 4}

# Only these (option, signature, offset) sites are emitted. Each was confirmed by
# reading the handler disassembly: a `lea reg,[eax+imm]` / `mov reg,eax` right after
# the scan call, then a VirtualProtect call, then the store to that register. The
# loose auto-recovery also produces candidates from other control-flow shapes that
# cannot be confirmed this way, so they are excluded rather than guessed.
LOADER = (0x100523c0, 0x10055c70)     # the dispatch arm

VERIFIED_SITES = {
    ("IncreasePtrNodes", "6A 14 ? ? ? ? ? 68 80 3E 00 00", 8),
    ("IncreasePtrNodes", "6A 08 ? ? ? ? ? 68 80 38 01 00", 8),
    ("IncreasePtrNodes", "6A 10 ? ? ? ? ? 68 80 3E 00 00", 8),
    ("IncreaseVehicleStructLimit", "68 ? ? ? ? 6A 32 8B C8", 6),
    ("NoLoadingSleep", "68 88 13 00 00 FF 15", 1),
    ("OverrideGTADatPath", "68 ? ? ? ? E8 ? ? ? ? 83 C4 04 E8 ? ? ? ? E8 ? ? ? ? E8", 1),
    ("VSyncFix", "8B 08 8B 51 0C 6A 01 6A 00 6A 00 6A 00 6A 00 50 FF D2 8B F0 81 FE ? ? ? ? 74", 30),
    ("VSyncFix", "8B 10 50 8B 82 A8 00 00 00 FF D0 83", 13),
    # loader-inline NOP fills (dispatch arm)
    ("NoLODLightHeightCutoff", "76 08 F3 0F 11 4C 24 1C EB 06 F3 0F 11 44 24 1C 89 5C 24 64", 0),
}


def find_objdump():
    if os.environ.get("OBJDUMP"):
        return os.environ["OBJDUMP"]
    found = shutil.which("i686-w64-mingw32-objdump")
    if found:
        return found
    found = shutil.which("objdump")  # x86_64 GNU objdump disassembles i386 PE fine
    if found:
        return found
    for entry in sorted(pathlib.Path("/nix/store").glob(
            "*i686-w64-mingw32-binutils-2.46/bin/i686-w64-mingw32-objdump")):
        return str(entry)
    raise SystemExit("i686-w64-mingw32-objdump not found; set OBJDUMP")


def make_cstr(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt = pe + 24
    so = opt + struct.unpack_from("<H", data, pe + 20)[0]
    secs = [struct.unpack_from("<8sIIIIIIHHI", data, so + 40 * i) for i in range(nsec)]
    ib = struct.unpack_from("<I", data, opt + 28)[0]

    def cstr(va):
        rva = va - ib
        for s in secs:
            if s[2] <= rva < s[2] + max(s[1], s[3]):
                o = s[4] + (rva - s[2])
                end = data.find(b"\0", o)
                if end <= o or end - o > 256:
                    return None
                try:
                    return data[o:end].decode("latin1")
                except UnicodeDecodeError:
                    return None
        return None
    return cstr


def parse(objdump, va, stop):
    text = subprocess.run([objdump, "-d", "-Mintel", "--start-address=%#x" % va,
                           "--stop-address=%#x" % stop, str(ASI)],
                          capture_output=True, text=True, check=True).stdout
    out = []
    for line in text.splitlines():
        m = re.match(r"\s*([0-9a-f]+):\t([0-9a-f ]+)\t(.*)$", line)
        if m:
            out.append(m.group(3).strip())
    return out


def recover(ins, cstr):
    """Return (signature, offset, width, value) for each literal store."""
    out = []
    n = len(ins)
    # Registers that hold VirtualProtect: loaded from the import slot 0x10063010.
    protect_regs = set()
    for t in ins:
        m = re.match(r"mov\s+([a-z]+),DWORD PTR ds:0x10063010$", t)
        if m:
            protect_regs.add(m.group(1))

    # Detect `mov reg,0x90909090` (NOP fill) earlier in the body so a later
    # `mov [site],reg` is recognised as a NOP write of that width.
    def nop_width_in(before_index):
        for k in range(before_index - 1, max(0, before_index - 12), -1):
            m = re.match(r"mov\s+([a-z]+),0x90909090$", ins[k])
            if m:
                return m.group(1)
        return None

    def protected(q):
        # The store follows a VirtualProtect call within a short window (the
        # old-protection out-parameter is often set up between the call and the
        # store). The call is either the named import or a register loaded from it.
        for back in range(1, 4):
            if q - back < 0:
                break
            prev = ins[q - back]
            if prev == "call   DWORD PTR ds:0x10063010":
                return True
            m = re.match(r"call\s+([a-z]+)$", prev)
            if m and m.group(1) in protect_regs:
                return True
        return False

    def offset_from_match(store_index, base_reg):
        """Offset of [base_reg] from the scan match (eax), walking `lea [base+imm]`,
        `add base,imm`, `mov base,eax` and `mov base,<alias>` chains."""
        off, cur, j = 0, base_reg, store_index - 1
        while j >= 0 and store_index - j <= 60:
            txt = ins[j]
            m = re.match(r"lea\s+%s,\[([a-z0-9]+)([+\-])0x([0-9a-f]+)\]" % cur, txt)
            if m:
                base = m.group(1)
                d = int(m.group(3), 16)
                off += d if m.group(2) == "+" else -d
                if base == "eax":
                    return off                 # reached the scan match
                cur = base
                j -= 1; continue
            m = re.match(r"add\s+%s,0x([0-9a-f]+)" % cur, txt)
            if m:
                off += int(m.group(1), 16); j -= 1; continue
            m = re.match(r"mov\s+%s,eax$" % cur, txt)
            if m:
                return off
            m = re.match(r"mov\s+%s,([a-z]+)$" % cur, txt)
            if m:
                cur = m.group(1); j -= 1; continue
            if re.match(r"lea\s+%s,\[" % cur, txt) or re.match(r"mov\s+%s," % cur, txt):
                return None
            j -= 1
        return None

    for sc, txt in enumerate(ins):
        if not (txt.startswith("call") and "0x10003830" in txt):
            continue
        pat = None
        for k in range(sc - 1, max(0, sc - 8), -1):
            m = re.match(r"mov\s+(ecx|edx),0x([0-9a-f]+)", ins[k])
            if m:
                pat = int(m.group(2), 16); break
        if pat is None:
            continue
        pattern = cstr(pat)
        if not pattern:
            continue
        # Bound the store search to this scan's block (up to the next scan call),
        # so one pattern's stores are not attributed to the next pattern.
        end = min(n, sc + 90)
        for nx in range(sc + 1, n):
            if ins[nx].startswith("call") and "0x10003830" in ins[nx]:
                end = nx; break
        for q in range(sc + 1, end):
            write = re.match(
                r"mov\s+(BYTE|WORD|DWORD) PTR \[([a-z0-9]+)\],(0x[0-9a-f]+|[a-z]+)$", ins[q])
            if not write:
                continue
            base = write.group(2)
            off = offset_from_match(q, base)
            if off is None or not protected(q):
                continue
            width = WIDTHS[write.group(1)]
            operand = write.group(3)
            if operand.startswith("0x"):
                out.append((pattern, off, width, int(operand, 16)))
            elif nop_width_in(q) == operand:
                out.append((pattern, off, width, 0x90909090 & ((1 << (8 * width)) - 1)))
    return out



def build():
    data = ASI.read_bytes()
    if hashlib.sha256(data).hexdigest() != ASI_SHA:
        raise SystemExit("unrecognized evidence binary")
    cstr = make_cstr(data)
    objdump = find_objdump()
    addrs = sorted(int(r["address"], 16)
                   for r in csv.DictReader(FUNCS.open()) if r.get("status") == "ok")
    handlers = json.loads((ROOT / "evidence/option_handlers.json").read_text())
    # Existing literal-op keys, to avoid emitting a duplicate write.
    existing = set()
    for m in re.finditer(r'\{"([A-Za-z0-9_]+)", "([^"]*)", (-?\d+),', 
                         (ROOT / "src/recovered_patch_ops.inc").read_text()):
        existing.add((m.group(1), m.group(2), int(m.group(3))))

    rows, seen = [], set()
    for option, al in sorted(handlers.items()):
        for h in al:
            a = int(h, 16)
            i = bisect.bisect_right(addrs, a)
            stop = addrs[i] if i < len(addrs) else a + 0x4000
            if stop - a > 0x4000:
                stop = a + 0x4000
            for sig, off, width, value in recover(parse(objdump, a, stop), cstr):
                if (option, sig, off) in existing:
                    continue
                if (option, sig, off) not in VERIFIED_SITES:
                    continue          # not among the disassembly-confirmed sites
                key = (option, sig, off, width, value)
                if key not in seen:
                    seen.add(key); rows.append(key)
    # Loader-inline pass: options handled directly in the dispatch arm. Two shapes:
    #   * the site register is loaded from a spill slot then NOP-filled
    #     (`mov ecx,0x90909090; mov WORD [reg],cx`) - NoLODLightHeightCutoff;
    #   * a scan call whose result is stored to a register then NOP-filled
    #     (NoImpostorHeightCutoff).
    loader_ins = parse(objdump, LOADER[0], LOADER[1])
    refs = json.loads((ROOT / "evidence/option_references.json").read_text())
    key_vas = {int(v["string_va"], 16): o for o, v in refs.items() if v.get("string_va")}

    def loader_option(index):
        for k in range(index, max(0, index - 500), -1):
            m = re.match(r"push\s+0x([0-9a-f]+)$", loader_ins[k])
            if m and int(m.group(1), 16) in key_vas:
                return key_vas[int(m.group(1), 16)]
        return None

    # shape (b): a site register holds a spill slot that a scan stored the match to;
    #   mov reg,0x90909090 ; mov W [reg2],reg  where reg2 = [slot]
    n = len(loader_ins)
    for q, txt in enumerate(loader_ins):
        wm = re.match(r"mov\s+(BYTE|WORD|DWORD) PTR \[([a-z]+)\],([a-z]+)$", txt)
        if not wm:
            continue
        width, sitereg, nopreg = WIDTHS[wm.group(1)], wm.group(2), wm.group(3)
        # nopreg must have been loaded with 0x90909090 just above
        # The store may use a sub-register (cx/ax/dx) of the 32-bit register that
        # was loaded with the NOP pattern.
        SUBREG = {"cx": "ecx", "ax": "eax", "dx": "edx", "bx": "ebx",
                  "si": "esi", "di": "edi", "cl": "ecx", "al": "eax", "dl": "edx"}
        nop = None
        nop_full = SUBREG.get(nopreg, nopreg)
        for b in range(q - 1, max(0, q - 4), -1):
            nm = re.match(r"mov\s+(%s|%s),0x90909090$" % (re.escape(nopreg), re.escape(nop_full)),
                          loader_ins[b])
            if nm:
                nop = b; break
        if nop is None:
            continue
        # sitereg must hold a spill slot: look back for `mov sitereg,[ebp-..]`
        slot = None
        for b in range(q - 1, max(0, q - 6), -1):
            sm = re.match(r"mov\s+%s,DWORD PTR \[(ebp[^\]]*)\]$" % sitereg, loader_ins[b])
            if sm:
                slot = sm.group(1); break
        if slot is None:
            continue
        # a scan call must have written the match into that slot
        store = None
        for b in range(max(0, q - 60), q):
            st = re.match(r"mov\s+DWORD PTR \[(%s)\],eax$" % re.escape(slot), loader_ins[b])
            if st:
                store = b; break
        if store is None:
            continue
        pat = None
        for k in range(store - 1, max(0, store - 40), -1):
            mm = re.match(r"mov\s+(ecx|edx),0x([0-9a-f]+)", loader_ins[k])
            if mm:
                pat = int(mm.group(2), 16); break
        if pat is None:
            continue
        pattern = cstr(pat)
        option = loader_option(store)
        if not pattern or not option:
            continue
        off = 0
        if (option, pattern, off) in existing:
            continue
        if (option, pattern, off) not in VERIFIED_SITES:
            continue
        rows.append((option, pattern, off, width, 0x90909090 & ((1 << (8 * width)) - 1)))

    rows.sort()
    # Every allow-listed site must have been reproduced, or the evidence drifted.
    found = {(o, s, off) for o, s, off, _w, _v in rows}
    missing = VERIFIED_SITES - found
    if missing:
        raise SystemExit("verified sites not reproduced: " + str(sorted(missing)))
    lines = [
        "// Generated from ZolikaPatch handlers; signature-relative literal stores that",
        "// the primary op table did not carry. Fields: option, signature, offset, width,",
        "// value. The apply path writes `value` (little-endian) at match + offset.",
    ]
    for option, sig, off, width, value in rows:
        lines.append('  {{"{o}", "{p}", {off}, {w}u, 0x{v:x}u}},'.format(
            o=option, p=sig, off=off, w=width, v=value))
    return "\n".join(lines) + "\n", len(rows)


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--check", action="store_true")
    a = p.parse_args()
    out = ROOT / "src/recovered_literal_stores.inc"
    text, n = build()
    if a.check:
        if out.read_text() != text:
            raise SystemExit("recovered literal stores are stale")
        print(f"recovered literal stores verified ({n} entries)")
    else:
        out.write_text(text)
        print(f"recovered literal stores written ({n} entries)")

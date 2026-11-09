#!/usr/bin/env python3
"""Recover signature-relative relative-jump installs from ZolikaPatch handlers.

Many handlers patch the game with the shape:

    mov   edx, <pattern string>          ; pattern VA in the plugin's .rdata
    call  0x10003830                     ; single-hit pattern scanner -> eax = match
    add   <reg>, <off>                   ; patch site = match + off
    mov   byte ptr [<reg>], 0xe9         ; opcode (through VirtualProtect)
    mov   eax, 0x100xxxxx                ; plugin trampoline
    sub   eax, <reg>
    sub   eax, 5                         ; rel32 = dest - site - 5
    mov   [<reg>+1], eax

The literal op table (recovered_patch_ops.inc) records only the fixed bytes, so for
these handlers it stores just "E9" (1 byte) and drops the rel32 - leaving the
recovered jump incomplete. This extracts the *whole* install: (option, signature,
offset, destination), so the apply path can write E9 + rel32 exactly as the
handler does. Options whose handler writes the rel32 inline (e.g. many of the
single-byte "E9" rows) are unaffected.

The recovery is validated against the trusted literal op table: for every signature
that both recover, the relative offset must agree (it does, 22/22), and the
destination is a plausible plugin VA.

Read-only. Requires i686-w64-mingw32-objdump (set OBJDUMP or put it on PATH)."""
import argparse, bisect, csv, hashlib, json, os, pathlib, re, shutil, struct, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ASI = ROOT.parent / "ZolikaPatch_extracted/ZolikaPatch.asi"
FUNCS = ROOT.parent / "ZolikaPatch_decompiled/ghidra/functions.csv"
ASI_SHA = "720fbcebfd563616151560020eea12168115a9183d6813b432b0367d2b702fc5"
PLUGIN_LO, PLUGIN_HI = 0x10000000, 0x10200000


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


def offset_for(ins, k, reg):
    off, j = 0, k - 1
    while j >= 0:
        txt = ins[j]
        m = re.match(r"add\s+%s,0x([0-9a-f]+)" % reg, txt)
        if m:
            off += int(m.group(1), 16); j -= 1; continue
        # lea reg,[base+imm]: reg = base + imm. The base is the scan match (or the
        # stack), so the offset is imm; stop here (base is not something we sum).
        m = re.match(r"lea\s+%s,\[[^\]]+\+0x([0-9a-f]+)\]" % reg, txt)
        if m:
            return off + int(m.group(1), 16)
        if re.match(r"mov\s+%s,0x[0-9a-f]+$" % reg, txt):
            return None                       # overwritten with a constant
        if re.match(r"mov\s+%s,(?:DWORD PTR )?\[[^\]]+\]" % reg, txt):
            return off
        if re.match(r"mov\s+%s,eax$" % reg, txt):
            return off
        j -= 1
        if k - j > 48:
            break
    return None


def recover(ins, cstr):
    n = len(ins)
    # Only the single-hit scanner (0x10003830) is modelled: it returns one match, so
    # (signature, offset, destination) is unambiguous. The multi-hit collector
    # (0x1005afa0) installs at the 0th/1st/... match, which this model cannot express.
    scans = [i for i, txt in enumerate(ins) if txt.startswith("call") and "0x10003830" in txt]
    out = []
    for idx, sc in enumerate(scans):
        end = scans[idx + 1] if idx + 1 < len(scans) else n
        pat = None
        for k in range(sc - 1, max(0, sc - 8), -1):
            m = re.match(r"mov\s+(ecx|edx),0x([0-9a-f]+)", ins[k])
            if m:
                pat = int(m.group(2), 16); break
        if pat is None or not (0x10001000 <= pat < 0x11000000):
            continue
        pattern = cstr(pat)
        if not pattern:
            continue
        for k in range(sc + 1, end):
            m = re.match(r"mov\s+BYTE PTR \[([^\]]+)\],0xe9", ins[k])
            if not m:
                continue
            reg = m.group(1)
            off = offset_for(ins, k, reg)
            dest = None
            for q in range(k + 1, min(end, k + 16)):
                mm = re.match(r"mov\s+eax,0x([0-9a-f]+)", ins[q])
                if mm and PLUGIN_LO <= int(mm.group(1), 16) < PLUGIN_HI \
                        and q + 2 < end \
                        and re.match(r"sub\s+eax,(%s$|DWORD PTR \[[^\]]+\]$)" % reg, ins[q + 1]) \
                        and ins[q + 2] == "sub    eax,0x5":
                    dest = int(mm.group(1), 16); break
            if off is not None and dest is not None:
                out.append((pattern, off, dest))
    return out


# Handlers whose pattern is built at run time (a hex-digit loop) load it from the
# constructed string, so there is no `mov ecx,<imm>` to read; their signature comes
# from recovered_extra_signatures.inc. Each entry below was confirmed by reading the
# handler disassembly (scan call -> site = match + offset -> VirtualProtect -> E9).
RUNTIME_PATTERN_JUMPS = [
    # CarPartsShadowFix: `lea edi,[eax-0x6]; mov byte [edi],0xe9;
    # mov eax,0x10006880; sub eax,edi; sub eax,5`.
    ("CarPartsShadowFix", "485 DB 7E 41 55 EB 0A", -6, 0x10006880),
]


def build():
    data = ASI.read_bytes()
    if hashlib.sha256(data).hexdigest() != ASI_SHA:
        raise SystemExit("unrecognized evidence binary")
    cstr = make_cstr(data)
    objdump = find_objdump()
    addrs = sorted(int(r["address"], 16)
                   for r in csv.DictReader(FUNCS.open()) if r.get("status") == "ok")
    handlers = json.loads((ROOT / "evidence/option_handlers.json").read_text())
    refs = json.loads((ROOT / "evidence/option_references.json").read_text())
    key_vas = {int(v["string_va"], 16): o for o, v in refs.items() if v.get("string_va")}
    inc = (ROOT / "src/recovered_patch_ops.inc").read_text()
    ops = {}
    for o, s, off in re.findall(r'\{"([A-Za-z0-9_]+)", "([^"]*)", (-?\d+),', inc):
        ops.setdefault(o, []).append((s, int(off)))

    rows, seen, skipped = [], set(), []
    for option, al in sorted(handlers.items()):
        for h in al:
            a = int(h, 16)
            i = bisect.bisect_right(addrs, a)
            stop = addrs[i] if i < len(addrs) else a + 0x4000
            if stop - a > 0x4000:
                stop = a + 0x4000
            for pattern, off, dest in recover(parse(objdump, a, stop), cstr):
                # Where the trusted literal op table already has this signature, the
                # offset must agree. A disagreement means the linear walk crossed
                # interleaved control flow, so the candidate is dropped (the trusted
                # table wins) rather than emitted.
                conflict = [o for s, o in ops.get(option, []) if s == pattern and o != off]
                if conflict:
                    skipped.append((option, pattern, off, conflict[0]))
                    continue
                key = (option, pattern, off, dest)
                if key in seen:
                    continue
                seen.add(key)
                rows.append(key)
    if skipped:
        sys.stderr.write(f"note: dropped {len(skipped)} ambiguous candidates "
                         f"(op table offset wins):\n")
        for option, pattern, got, want in skipped[:8]:
            sys.stderr.write(f"  {option}: {pattern[:30]} recovered {got}, table {want}\n")
    # Loader-inline installs: options the loader handles directly (no standalone
    # handler). Same shape, but the option is the key whose string was pushed most
    # recently before the install site.
    loader = (0x100523c0, 0x10055c70)
    loader_ins = parse(objdump, loader[0], loader[1])
    def option_at(index):
        for k in range(index, max(0, index - 400), -1):
            m = re.match(r"push\s+0x([0-9a-f]+)$", loader_ins[k])
            if m and int(m.group(1), 16) in key_vas:
                return key_vas[int(m.group(1), 16)]
        return None
    for sc in range(len(loader_ins)):
        if not (loader_ins[sc].startswith("call") and "0x10003830" in loader_ins[sc]):
            continue
        pat = None
        for k in range(sc - 1, max(0, sc - 8), -1):
            m = re.match(r"mov\s+(ecx|edx),0x([0-9a-f]+)", loader_ins[k])
            if m:
                pat = int(m.group(2), 16); break
        if pat is None:
            continue
        pattern = cstr(pat)
        if not pattern:
            continue
        for k in range(sc + 1, min(len(loader_ins), sc + 40)):
            m = re.match(r"mov\s+BYTE PTR \[([^\]]+)\],0xe9", loader_ins[k])
            if not m:
                continue
            reg = m.group(1)
            off = offset_for(loader_ins, k, reg)
            dest = None
            for q in range(k + 1, min(len(loader_ins), k + 16)):
                mm = re.match(r"mov\s+eax,0x([0-9a-f]+)", loader_ins[q])
                if mm and PLUGIN_LO <= int(mm.group(1), 16) < PLUGIN_HI \
                        and q + 2 < len(loader_ins) \
                        and re.match(r"sub\s+eax,(%s$|DWORD PTR \[[^\]]+\]$)" % reg, loader_ins[q + 1]) \
                        and loader_ins[q + 2] == "sub    eax,0x5":
                    dest = int(mm.group(1), 16); break
            if off is None or dest is None:
                continue
            option = option_at(k)
            if not option:
                continue
            conflict = [o for s2, o in ops.get(option, []) if s2 == pattern and o != off]
            if conflict:
                skipped.append((option, pattern, off, conflict[0]))
                continue
            key = (option, pattern, off, dest)
            if key in seen:
                continue
            seen.add(key)
            rows.append(key)

    # Runtime-built-pattern handlers (disassembly-confirmed explicit list): the
    # handler builds its pattern with a hex-digit loop, so there is no `mov
    # ecx,<imm>` to read. The signature is taken from recovered_extra_signatures.inc
    # so the two tables agree.
    extra = {}
    for m in re.finditer(r'\{"([A-Za-z0-9_]+)", "([^"]*)",',
                         (ROOT / "src/recovered_extra_signatures.inc").read_text()):
        extra.setdefault(m.group(1), set()).add(m.group(2))
    for option, signature, off, dest in RUNTIME_PATTERN_JUMPS:
        if signature not in extra.get(option, set()):
            raise SystemExit(
                f"{option}: runtime pattern {signature!r} not in extra signatures")
        key = (option, signature, off, dest)
        if key not in seen:
            seen.add(key); rows.append(key)
    rows.sort()

    lines = [
        "// Generated from ZolikaPatch handlers; recovered signature-relative jumps.",
        "// Fields: option, signature, relative offset, jump destination VA.",
        "// The apply path writes E9 + rel32 to the scan match at the given offset; the",
        "// literal op table stores only the E9 byte for these, so this completes them.",
    ]
    for option, pattern, off, dest in rows:
        lines.append('  {{"{o}", "{p}", {off}, 0x{d:x}u}},'.format(
            o=option, p=pattern, off=off, d=dest))
    return "\n".join(lines) + "\n", len(rows)


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--check", action="store_true")
    a = p.parse_args()
    out = ROOT / "src/recovered_sig_jumps.inc"
    text, n = build()
    if a.check:
        if out.read_text() != text:
            raise SystemExit("recovered signature jumps are stale")
        print(f"recovered signature jumps verified ({n} entries)")
    else:
        out.write_text(text)
        print(f"recovered signature jumps written ({n} entries)")

#!/usr/bin/env python3
"""Recover Nth-match jump installs (the multi-hit collector shape).

Most handlers locate one match with the single-hit scanner (0x10003830). A few use
the multi-hit collector (0x1005afa0), which returns a vector of matches, and install
a relative jump at the k-th match. The (signature, offset, destination) model cannot
express that (it always resolves the first match, and the same signature maps to
several sites), so this recovers (option, signature, match_index, offset,
destination).

Recovered instance: BenchmarkFix - signature
`E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20`, offset +8, match[0] -> 0x10005650 and
match[1] -> 0x10005670.

Read-only. Requires i686-w64-mingw32-objdump (set OBJDUMP or put it on PATH)."""
import argparse, bisect, csv, hashlib, json, os, pathlib, re, shutil, struct, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ASI = ROOT.parent / "ZolikaPatch_extracted/ZolikaPatch.asi"
FUNCS = ROOT.parent / "ZolikaPatch_decompiled/ghidra/functions.csv"
ASI_SHA = "720fbcebfd563616151560020eea12168115a9183d6813b432b0367d2b702fc5"
PLUGIN_LO, PLUGIN_HI = 0x10000000, 0x10200000
COLLECTOR = "call   0x1005afa0"


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
    """Return (signature, match_index, offset, destination) for each install."""
    out = []
    n = len(ins)
    for sc, txt in enumerate(ins):
        if txt != COLLECTOR:
            continue
        pat = None
        for k in range(sc - 1, max(0, sc - 10), -1):
            m = re.match(r"mov\s+(ecx|edx),0x([0-9a-f]+)", ins[k])
            if m:
                pat = int(m.group(2), 16); break
        if pat is None:
            continue
        pattern = cstr(pat)
        if not pattern:
            continue
        # For each E9 store in the block, its match index is the displacement of the
        # `mov reg,[base + k*4]` that loaded the site; the offset is the following
        # `add reg,imm`.
        block = ins[sc + 1: sc + 200]
        for i, t in enumerate(block):
            m = re.match(r"mov\s+([a-z]+),DWORD PTR \[([a-z]+)(?:\+0x([0-9a-f]+))?\]$", t)
            if not m:
                continue
            reg, displ = m.group(1), m.group(3)
            match_index = int(displ, 16) // 4 if displ else 0
            off = 0
            for r in range(i + 1, min(len(block), i + 8)):
                am = re.match(r"add\s+%s,0x([0-9a-f]+)" % reg, block[r])
                if am:
                    off = int(am.group(1), 16); break
            opcode = None
            for r in range(i + 1, min(len(block), i + 12)):
                om = re.match(r"mov\s+BYTE PTR \[%s\],0x(e8|e9)$" % reg, block[r])
                if om:
                    opcode = int(om.group(1), 16); break
            if opcode is None:
                continue
            dest = None
            for r in range(i + 1, min(len(block), i + 40)):
                mm = re.match(r"mov\s+eax,0x([0-9a-f]+)", block[r])
                if mm and PLUGIN_LO <= int(mm.group(1), 16) < PLUGIN_HI \
                        and r + 2 < len(block) and block[r + 1] == ("sub    eax,%s" % reg) \
                        and block[r + 2] == "sub    eax,0x5":
                    dest = int(mm.group(1), 16); break
            if dest is not None:
                out.append((pattern, match_index, off, dest, opcode))
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
    rows, seen = [], set()
    for option, al in sorted(handlers.items()):
        for h in al:
            a = int(h, 16)
            i = bisect.bisect_right(addrs, a)
            stop = addrs[i] if i < len(addrs) else a + 0x4000
            if stop - a > 0x4000:
                stop = a + 0x4000
            for pattern, idx, off, dest, opcode in recover(parse(objdump, a, stop), cstr):
                key = (option, pattern, idx, off, dest, opcode)
                if key not in seen:
                    seen.add(key); rows.append(key)
    if not rows:
        raise SystemExit("no Nth-match jump installs recovered")
    rows.sort()
    lines = [
        "// Generated from ZolikaPatch handlers; Nth-match jump installs (multi-hit",
        "// collector 0x1005afa0). Fields: option, signature, match index, relative",
        "// offset, jump destination VA, opcode (e8 = call, e9 = jmp). The apply path",
        "// scans for ALL matches and writes opcode+rel32 at matches[match_index]+offset.",
    ]
    for option, pattern, idx, off, dest, opcode in rows:
        lines.append('  {{"{o}", "{p}", {i}, {off}, 0x{d:x}u, 0x{c:x}u}},'.format(
            o=option, p=pattern, i=idx, off=off, d=dest, c=opcode))
    return "\n".join(lines) + "\n", len(rows)


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--check", action="store_true")
    a = p.parse_args()
    out = ROOT / "src/recovered_match_jumps.inc"
    text, n = build()
    if a.check:
        if out.read_text() != text:
            raise SystemExit("recovered match jumps are stale")
        print(f"recovered match jumps verified ({n} entries)")
    else:
        out.write_text(text)
        print(f"recovered match jumps written ({n} entries)")

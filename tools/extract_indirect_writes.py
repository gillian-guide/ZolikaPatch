#!/usr/bin/env python3
"""Recover indirect-literal writes from ZolikaPatch handlers and the loader.

A few options patch the game with the shape:

    mov   edx, <pattern>;  call 0x10003830   ; single-hit scan -> eax = match
    mov   <reg>, DWORD PTR [eax+off]         ; <reg> = the pointer the match names
    VirtualProtect(<reg>, width, 0x40, &old)
    mov   <W> PTR [<reg>], <value>           ; write through the indirect pointer

The literal op table cannot express the indirection (it writes at a fixed
signature+offset) and the jump tables cannot express a non-jump write, so these
ops were previously missing. This extracts (option, signature, pointer_offset,
width, value) and verifies each store is bracketed by a VirtualProtect call.

Read-only. Requires i686-w64-mingw32-objdump (set OBJDUMP or put it on PATH)."""
import argparse, bisect, csv, hashlib, json, os, pathlib, re, shutil, struct, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ASI = ROOT.parent / "ZolikaPatch_extracted/ZolikaPatch.asi"
FUNCS = ROOT.parent / "ZolikaPatch_decompiled/ghidra/functions.csv"
ASI_SHA = "720fbcebfd563616151560020eea12168115a9183d6813b432b0367d2b702fc5"
WIDTHS = {"BYTE": 1, "WORD": 2, "DWORD": 4}
LOADER = (0x100523c0, 0x10055c70)


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


def recover(ins, cstr, option_at):
    """Return (option, signature, pointer_offset, width, value) for each op."""
    out = []
    n = len(ins)

    def protected(i):
        return sum(1 for t in ins[max(0, i - 20):i + 20] if "0x10063010" in t) >= 2

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
        for k in range(sc + 1, min(n, sc + 45)):
            m = re.match(r"mov\s+([a-z]+),DWORD PTR \[eax([+\-]0x[0-9a-f]+)?\]$", ins[k])
            if not m:
                continue
            reg = m.group(1)
            pointer_offset = int(m.group(2), 16) if m.group(2) else 0
            for q in range(k + 1, min(n, k + 12)):
                sm = re.match(r"mov\s+(BYTE|WORD|DWORD) PTR \[%s\],(0x[0-9a-f]+)$" % reg, ins[q])
                if sm and protected(q):
                    option = option_at(sc)
                    if option:
                        out.append((option, pattern, pointer_offset,
                                    WIDTHS[sm.group(1)], int(sm.group(2), 16)))
                    break
            break
    return out


def build():
    data = ASI.read_bytes()
    if hashlib.sha256(data).hexdigest() != ASI_SHA:
        raise SystemExit("unrecognized evidence binary")
    cstr = make_cstr(data)
    objdump = find_objdump()
    refs = json.loads((ROOT / "evidence/option_references.json").read_text())
    key_vas = {int(v["string_va"], 16): o for o, v in refs.items() if v.get("string_va")}
    handlers = json.loads((ROOT / "evidence/option_handlers.json").read_text())
    addrs = sorted(int(r["address"], 16)
                   for r in csv.DictReader(FUNCS.open()) if r.get("status") == "ok")

    rows, seen = [], set()
    for option, al in sorted(handlers.items()):
        for h in al:
            a = int(h, 16)
            i = bisect.bisect_right(addrs, a)
            stop = addrs[i] if i < len(addrs) else a + 0x4000
            if stop - a > 0x4000:
                stop = a + 0x4000
            for r in recover(parse(objdump, a, stop), cstr, lambda _i, o=option: o):
                if r not in seen:
                    seen.add(r); rows.append(r)
    loader = parse(objdump, LOADER[0], LOADER[1])

    def loader_option(index):
        for k in range(index, max(0, index - 500), -1):
            m = re.match(r"push\s+0x([0-9a-f]+)$", loader[k])
            if m and int(m.group(1), 16) in key_vas:
                return key_vas[int(m.group(1), 16)]
        return None

    for r in recover(loader, cstr, loader_option):
        if r not in seen:
            seen.add(r); rows.append(r)

    rows.sort()
    lines = [
        "// Generated from ZolikaPatch handlers and the loader; indirect-literal writes.",
        "// Fields: option, signature, pointer offset (site = match + offset, then the",
        "// dword there is the target), width in bytes, value written.",
        "// Each store is bracketed by a VirtualProtect call in the recovered body.",
    ]
    for option, pattern, off, width, value in rows:
        lines.append('  {{"{o}", "{p}", {off}, {w}u, 0x{v:x}u}},'.format(
            o=option, p=pattern, off=off, w=width, v=value))
    return "\n".join(lines) + "\n", len(rows)


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--check", action="store_true")
    a = p.parse_args()
    out = ROOT / "src/recovered_indirect_writes.inc"
    text, n = build()
    if a.check:
        if out.read_text() != text:
            raise SystemExit("recovered indirect writes are stale")
        print(f"recovered indirect writes verified ({n} entries)")
    else:
        out.write_text(text)
        print(f"recovered indirect writes written ({n} entries)")

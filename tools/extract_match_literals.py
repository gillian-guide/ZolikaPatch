#!/usr/bin/env python3
"""Recover Nth-match literal/NOP writes (the multi-hit collector shape).

The collector 0x1005afa0 returns a vector of matches. Some handlers then write a
literal (or a NOP fill) at one or more of those matches - a shape the single-hit
literal table cannot express (the same signature maps to several sites). This
recovers (option, signature, match_index, offset, width, value):

  * match_index >= 0  -> matches[match_index]
  * match_index == -1 -> every match (the handler loops over the vector)

Recovered instances:
  * StartupTimeFix        - dword 0x101580d8 at match+6, ALL matches
  * NoImpostorHeightCutoff - word 0x9090 at match[0] and match[1]
  * NoLODLightHeightCutoff - word 0x9090 at match[0]

Read-only. Requires i686-w64-mingw32-objdump (set OBJDUMP or put it on PATH)."""
import argparse, bisect, csv, hashlib, json, os, pathlib, re, shutil, struct, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ASI = ROOT.parent / "ZolikaPatch_extracted/ZolikaPatch.asi"
FUNCS = ROOT.parent / "ZolikaPatch_decompiled/ghidra/functions.csv"
ASI_SHA = "720fbcebfd563616151560020eea12168115a9183d6813b432b0367d2b702fc5"
WIDTHS = {"BYTE": 1, "WORD": 2, "DWORD": 4}
COLLECTOR = "call   0x1005afa0"
LOADER = (0x100523c0, 0x10055c70)
# sub-register -> 32-bit name, so a `mov cx,0x9090` load is matched when the store
# uses the 16-bit sub-register of a register loaded with the NOP pattern.
SUBREG = {"cx": "ecx", "ax": "eax", "dx": "edx", "bx": "ebx",
          "si": "esi", "di": "edi", "cl": "ecx", "al": "eax", "dl": "edx"}

# Only these (option, signature, match_index, offset) sites are emitted. Each was
# confirmed by reading the handler disassembly: the multi-hit collector returns a
# vector, then a `mov reg,[vec+k*4]` (+ optional add) is followed by the write.
# The loose auto-recovery also produces candidates from unrelated control flow, so
# they are excluded rather than guessed.
VERIFIED_SITES = {
    # StartupTimeFix writes dword 0x101580d8 at match+6 for EVERY match of its
    # 3-hit pattern (the handler loops over the collector's vector). Confirmed by
    # reading the disassembly: `mov esi,[ecx+edi*4]; add esi,6; VirtualProtect;
    # mov DWORD PTR [esi],0x101580d8`.
    ("StartupTimeFix", "68 00 C2 EC 80 68", -1, 6),
    # NoImpostorHeightCutoff (loader-inline): NOP word 0x9090 at match[0] and match[1]
    # of its 2-hit pattern (`mov WORD PTR [esi],cx` / `[esi],ax`).
    ("NoImpostorHeightCutoff", "76 0B F3 0F 11 8C 24 88 00 00 00 EB 09", 0, 0),
    ("NoImpostorHeightCutoff", "76 0B F3 0F 11 8C 24 88 00 00 00 EB 09", 1, 0),
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
    """Return (signature, match_index, offset, width, value)."""
    out = []
    n = len(ins)

    for sc, txt in enumerate(ins):
        if txt != COLLECTOR:
            continue
        pat = None
        for k in range(sc - 1, max(0, sc - 12), -1):
            m = re.match(r"mov\s+(ecx|edx),0x([0-9a-f]+)", ins[k])
            if m:
                pat = int(m.group(2), 16); break
        if pat is None:
            continue
        pattern = cstr(pat)
        if not pattern:
            continue
        # In the block after the collector, find each site load and its write.
        for i in range(sc + 1, min(n, sc + 120)):
            load = re.match(
                r"mov\s+([a-z]+),DWORD PTR \[([a-z]+)(?:\+([a-z]+)\*4|\+0x([0-9a-f]+))?\]$",
                ins[i])
            if not load:
                continue
            reg, _base, idx_reg, displ = load.group(1), load.group(2), load.group(3), load.group(4)
            if idx_reg:
                match_index = -1                       # looping over every match
            elif displ:
                match_index = int(displ, 16) // 4
            else:
                match_index = 0
            # offset: a following `add reg,imm`
            off = 0
            for r in range(i + 1, min(n, i + 8)):
                am = re.match(r"add\s+%s,0x([0-9a-f]+)" % reg, ins[r])
                if am:
                    off = int(am.group(1), 16); break
                if re.match(r"mov\s+(BYTE|WORD|DWORD) PTR \[%s\]," % reg, ins[r]):
                    break
            # the write: `mov <W> [reg],<imm>` or `mov <W> [reg],<reg2>` (NOP)
            for q in range(i + 1, min(n, i + 16)):
                wm = re.match(r"mov\s+(BYTE|WORD|DWORD) PTR \[%s\],(0x[0-9a-f]+|[a-z]+)$" % reg,
                              ins[q])
                if not wm:
                    continue
                width = WIDTHS[wm.group(1)]
                operand = wm.group(2)
                value = None
                if operand.startswith("0x"):
                    value = int(operand, 16)
                else:
                    # `mov <W> [site],<reg>` after `mov <reg>,0x90909090`: NOP fill.
                    full = SUBREG.get(operand, operand)
                    for b in range(q - 1, max(0, q - 12), -1):
                        nm = re.match(r"mov\s+(%s|%s),0x90909090$"
                                      % (re.escape(operand), re.escape(full)), ins[b])
                        if nm:
                            value = 0x90909090 & ((1 << (8 * width)) - 1); break
                if value is not None:
                    out.append((pattern, match_index, off, width, value))
                break
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
    # The multi-hit *jump* table already owns E9 stores; only non-E9 writes here.
    rows, seen = [], set()
    for option, al in sorted(handlers.items()):
        for h in al:
            a = int(h, 16)
            i = bisect.bisect_right(addrs, a)
            stop = addrs[i] if i < len(addrs) else a + 0x4000
            if stop - a > 0x4000:
                stop = a + 0x4000
            for pattern, idx, off, width, value in recover(parse(objdump, a, stop), cstr):
                if value & 0xff == 0xe9 and width == 1:
                    continue                    # a jump, owned by the match-jump table
                if (option, pattern, idx, off) not in VERIFIED_SITES:
                    continue                    # not a disassembly-confirmed site
                key = (option, pattern, idx, off, width, value)
                if key not in seen:
                    seen.add(key); rows.append(key)
    # Loader-inline pass: options handled directly in the dispatch arm, attributed by
    # the most recent key-string push.
    loader_ins = parse(objdump, LOADER[0], LOADER[1])
    refs = json.loads((ROOT / "evidence/option_references.json").read_text())
    key_vas = {int(v["string_va"], 16): o for o, v in refs.items() if v.get("string_va")}

    def loader_option(index):
        for k in range(index, max(0, index - 500), -1):
            m = re.match(r"push\s+0x([0-9a-f]+)$", loader_ins[k])
            if m and int(m.group(1), 16) in key_vas:
                return key_vas[int(m.group(1), 16)]
        return None

    for sc, txt in enumerate(loader_ins):
        if txt != COLLECTOR:
            continue
        pat = None
        for k in range(sc - 1, max(0, sc - 12), -1):
            m = re.match(r"mov\s+(ecx|edx),0x([0-9a-f]+)", loader_ins[k])
            if m:
                pat = int(m.group(2), 16); break
        if pat is None:
            continue
        pattern = cstr(pat)
        if not pattern:
            continue
        option = loader_option(sc)
        if not option:
            continue
        for i in range(sc + 1, min(len(loader_ins), sc + 200)):
            load = re.match(
                r"mov\s+([a-z]+),DWORD PTR \[([a-z]+)(?:\+([a-z]+)\*4|\+0x([0-9a-f]+))?\]$",
                loader_ins[i])
            if not load:
                continue
            reg = load.group(1)
            if load.group(3):
                m_index = -1
            elif load.group(4):
                m_index = int(load.group(4), 16) // 4
            else:
                m_index = 0
            for q in range(i + 1, min(len(loader_ins), i + 16)):
                wm = re.match(r"mov\s+(BYTE|WORD|DWORD) PTR \[%s\],(0x[0-9a-f]+|[a-z]+)$" % reg,
                              loader_ins[q])
                if not wm:
                    continue
                width = WIDTHS[wm.group(1)]
                operand = wm.group(2)
                value = None
                if operand.startswith("0x"):
                    value = int(operand, 16)
                else:
                    full = SUBREG.get(operand, operand)
                    for b in range(q - 1, max(0, q - 12), -1):
                        nm = re.match(r"mov\s+(%s|%s),0x90909090$"
                                      % (re.escape(operand), re.escape(full)), loader_ins[b])
                        if nm:
                            value = 0x90909090 & ((1 << (8 * width)) - 1); break
                if value is not None and (option, pattern, m_index, 0) in VERIFIED_SITES:
                    key = (option, pattern, m_index, 0, width, value)
                    if key not in seen:
                        seen.add(key); rows.append(key)
                break

    rows.sort()
    found = {(o, s, i, off) for o, s, i, off, _w, _v in rows}
    missing = VERIFIED_SITES - found
    if missing:
        raise SystemExit("verified sites not reproduced: " + str(sorted(missing)))
    lines = [
        "// Generated from ZolikaPatch handlers; Nth-match literal/NOP writes.",
        "// Fields: option, signature, match index (-1 = every match), relative offset,",
        "// width, value. The apply path scans for ALL matches and writes at",
        "// matches[match_index] + offset (or every match when the index is -1).",
    ]
    for option, pattern, idx, off, width, value in rows:
        lines.append('  {{"{o}", "{p}", {i}, {off}, {w}u, 0x{v:x}u}},'.format(
            o=option, p=pattern, i=idx, off=off, w=width, v=value))
    return "\n".join(lines) + "\n", len(rows)


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--check", action="store_true")
    a = p.parse_args()
    out = ROOT / "src/recovered_match_literals.inc"
    text, n = build()
    if a.check:
        if out.read_text() != text:
            raise SystemExit("recovered match literals are stale")
        print(f"recovered match literals verified ({n} entries)")
    else:
        out.write_text(text)
        print(f"recovered match literals written ({n} entries)")

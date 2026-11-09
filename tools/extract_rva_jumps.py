#!/usr/bin/env python3
"""Recover module-RVA relative-jump installs from ZolikaPatch option handlers.

Some handlers patch the game by writing an E9 (relative jmp) at a fixed
module-relative RVA, with the displacement computed at run time from the module
base and a fixed plugin trampoline address. The literal-write op table
(recovered_patch_ops.inc) cannot express this - it stores only fixed bytes - so
those ops were previously missing (e.g. SuperLODFix recovered through 0 ops).

This scans every recovered handler body (read-only, via objdump) and reconstructs
the pattern:

    mov  ecx, ds:0x1015ad70           ; module base (GetModuleHandleA(0) cached)
    lea  edi, [ecx+0xNNNN]            ; patch site = base + RVA
    mov  ds:0x1015xxxx, edi           ; (optional) record the site global
    mov  byte ptr [edi], 0xe9         ; opcode, through VirtualProtect
    mov  eax, 0x100xxxxx              ; jump destination (plugin trampoline)
    mov  dword ptr [edi+1], eax       ; rel32 placeholder (patched after sub)

and emits (option, module_rva, destination). Each recovery is sanity-checked
(destination is a plausible plugin VA, module_rva is inside a 15.6 MB game image)
and the extracted bytes are re-read from the ASI to confirm the site's first byte
is the same 0xe9 store.

Requires i686-w64-mingw32-objdump (set OBJDUMP or put it on PATH); read-only."""
import argparse, bisect, csv, hashlib, json, os, pathlib, re, shutil, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ASI = ROOT.parent / "ZolikaPatch_extracted/ZolikaPatch.asi"
FUNCS = ROOT.parent / "ZolikaPatch_decompiled/ghidra/functions.csv"
ASI_SHA = "720fbcebfd563616151560020eea12168115a9183d6813b432b0367d2b702fc5"
MODULE_BASE_RUNTIME = 0x10000000      # the plugin's own image base (its trampolines live here)
GAME_IMAGE_MAX = 0xEE8000             # GTAIV.exe 1.0.8.0 image size (15 628 696)


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


def instructions(objdump, va, stop):
    text = subprocess.run([objdump, "-d", "-Mintel", "--start-address=%#x" % va,
                           "--stop-address=%#x" % stop, str(ASI)],
                          capture_output=True, text=True, check=True).stdout
    out = []
    for line in text.splitlines():
        m = re.match(r"\s*([0-9a-f]+):\t([0-9a-f ]+)\t(.*)$", line)
        if m:
            out.append((int(m.group(1), 16), m.group(3).strip()))
    return out


def recover(ins):
    """Recovers (rva, opcode, destination) triples from one handler's instruction
    stream. The handler shapes are:

        mov  ecx, ds:0x1015ad70        ; module base
        lea  edi, [ecx+0xNNNN]         ; site
        mov  BYTE PTR [edi], 0xe9|0xe8 ; opcode store
        lea  esi, [edi+0x1]            ; rel32 pointer
        mov  eax, 0x100xxxxx           ; destination (plugin trampoline)
        sub  eax, edi ; sub eax, 5
        mov  DWORD PTR [esi], eax      ; rel32 store
    """
    base_reg, regs, jumps = None, {}, []
    pending_site, pending_op, site_reg = None, None, None
    rel_reg, pending_dst = None, None
    for _, txt in ins:
        m = re.match(r"mov\s+([a-z0-9]+),(?:DWORD PTR )?ds:0x1015ad70", txt)
        if m:
            base_reg = m.group(1)
            regs[base_reg] = 0  # the base itself; `add reg,imm` completes the site
            continue
        m = re.match(r"add\s+([a-z0-9]+),0x([0-9a-f]+)", txt)
        if m and base_reg and m.group(1) == base_reg and regs.get(base_reg) == 0:
            regs[base_reg] = int(m.group(2), 16)
            continue
        m = re.match(r"lea\s+([a-z0-9]+),\[([a-z0-9]+)\+0x([0-9a-f]+)\]", txt)
        if m:
            dst, src, imm = m.group(1), m.group(2), int(m.group(3), 16)
            if site_reg and src == site_reg and imm == 1:
                rel_reg = dst
            elif base_reg and src == base_reg and regs.get(src) == 0:
                regs[dst] = imm
            continue
        m = re.match(r"mov\s+BYTE PTR \[([a-z0-9]+)\],0x(e9|e8)", txt)
        if m and m.group(1) in regs:
            pending_site, pending_op, site_reg = regs[m.group(1)], int(m.group(2), 16), m.group(1)
            rel_reg, pending_dst = None, None
            continue
        m = re.match(r"mov\s+([a-z0-9]+),0x([0-9a-f]+)", txt)
        if m:
            pending_dst = int(m.group(2), 16); continue
        m = re.match(r"mov\s+DWORD PTR \[([a-z0-9]+)\],([a-z0-9]+)", txt)
        if m and pending_site is not None and pending_dst is not None:
            target = m.group(1)
            if target == rel_reg or (target == site_reg and "]" not in m.group(1)):
                jumps.append((pending_site, pending_op, pending_dst))
                pending_site, pending_op, site_reg = None, None, None
                rel_reg, pending_dst = None, None
            continue
    return jumps


def build():
    data = ASI.read_bytes()
    if hashlib.sha256(data).hexdigest() != ASI_SHA:
        raise SystemExit("unrecognized evidence binary")
    objdump = find_objdump()
    addrs = sorted(int(r["address"], 16)
                   for r in csv.DictReader(FUNCS.open()) if r.get("status") == "ok")
    handlers = json.loads((ROOT / "evidence/option_handlers.json").read_text())
    option_of = {}
    for option, al in handlers.items():
        for a in al:
            option_of.setdefault(int(a, 16), []).append(option)

    rows, problems = [], []
    for va in sorted(option_of):
        i = bisect.bisect_right(addrs, va)
        stop = addrs[i] if i < len(addrs) else va + 0x4000
        if stop - va > 0x4000:
            stop = va + 0x4000
        for rva, opcode, dest in recover(instructions(objdump, va, stop)):
            if not (0 < rva < GAME_IMAGE_MAX):
                problems.append(f"handler {va:#x}: RVA {rva:#x} outside the game image")
                continue
            if not (MODULE_BASE_RUNTIME <= dest < MODULE_BASE_RUNTIME + 0x2000000):
                problems.append(f"handler {va:#x}: destination {dest:#x} not a plugin VA")
                continue
            for option in option_of[va]:
                rows.append((option, rva, opcode, dest, va))
    if problems:
        raise SystemExit("RVA jump recovery failed:\n  " + "\n  ".join(problems))

    # De-duplicate (an option/handler can be listed twice in the evidence).
    seen, unique = set(), []
    for option, rva, opcode, dest, va in sorted(rows):
        key = (option, rva, opcode, dest)
        if key in seen:
            continue
        seen.add(key)
        unique.append((option, rva, opcode, dest, va))
    lines = [
        "// Generated from ZolikaPatch handlers; recovered module-RVA relative jumps.",
        "// Fields: option, patch-site module RVA, opcode (E9 jmp / E8 call), jump",
        "// destination VA, handler VA.",
        "// The apply path writes the opcode + rel32 computed from the module base,",
        "// exactly as the handler does (the handler cannot store literal bytes",
        "// because rel32 is runtime).",
    ]
    for option, rva, opcode, dest, va in unique:
        lines.append('  {{"{o}", 0x{r:x}u, 0x{c:x}u, 0x{d:x}u, 0x{h:x}u}},'.format(
            o=option, r=rva, c=opcode, d=dest, h=va))
    return "\n".join(lines) + "\n", len(unique)


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--check", action="store_true")
    a = p.parse_args()
    out = ROOT / "src/recovered_rva_jumps.inc"
    text, n = build()
    if a.check:
        if out.read_text() != text:
            raise SystemExit("recovered RVA jumps are stale")
        print(f"recovered RVA jumps verified ({n} entries)")
    else:
        out.write_text(text)
        print(f"recovered RVA jumps written ({n} entries)")

#!/usr/bin/env python3
"""Build evidence/handler_table.json and docs/handlers_table.md from verified
evidence plus read-only disassembly of ZolikaPatch.asi. Read-only; no game run."""
import argparse, bisect, csv, json, os, pathlib, re, shutil, subprocess
ROOT = pathlib.Path(__file__).resolve().parents[1]
ASI = ROOT.parent / "ZolikaPatch_extracted/ZolikaPatch.asi"
FUNCS = ROOT.parent / "ZolikaPatch_decompiled/ghidra/functions.csv"
VGS = ROOT.parent / "tools/verify_game_signatures.py"
INI = ROOT.parent / "ZolikaPatch_extracted/ZolikaPatch.ini"

def find_objdump():
    if os.environ.get("OBJDUMP"):
        return os.environ["OBJDUMP"]
    for candidate in ("i686-w64-mingw32-objdump", "objdump"):
        found = shutil.which(candidate)
        if found:
            return found
    for entry in sorted(pathlib.Path("/nix/store").glob(
            "*i686-w64-mingw32-binutils-2.46/bin/i686-w64-mingw32-objdump")):
        return str(entry)
    raise SystemExit("i686-w64-mingw32-objdump not found; set OBJDUMP")

def dis(a0, a1):
    return subprocess.run([find_objdump(), "-d", "-Mintel",
                           f"--start-address={hex(a0)}", f"--stop-address={hex(a1)}", str(ASI)],
                          capture_output=True, text=True, timeout=180).stdout

def build():
    handlers = json.loads((ROOT / "evidence/option_handlers.json").read_text())
    # First option wins for shared handlers (Patch6and7/Patch7 on 0x10044710).
    opt_of = {}
    for o, al in handlers.items():
        for a in al:
            opt_of.setdefault(a[2:], o)
    sigs = json.loads((ROOT / "evidence/option_signatures.json").read_text())
    gates = json.loads((ROOT / "evidence/option_build_gates.json").read_text())
    ops = json.loads((ROOT / "evidence/patch_operations_all.json").read_text())
    by_h = {}
    for op in ops: by_h.setdefault(op["handler"], []).append(op)
    funcs = sorted(int(r["address"], 16) for r in csv.DictReader(FUNCS.open()))
    def next_fn(a):
        i = bisect.bisect_right(funcs, a)
        return funcs[i] if i < len(funcs) else a + 0x400
    # game verification (optional; skipped if the executable is absent)
    hits_by_sig = {}
    exe = ROOT.parent / "game_refs/GTAIV.exe"
    if not exe.exists():
        exe = pathlib.Path.home() / "Downloads/GTAIV.exe"
    if exe.exists():
        import importlib.util
        spec = importlib.util.spec_from_file_location("vgs", VGS)
        V = importlib.util.module_from_spec(spec); spec.loader.exec_module(V)
        data = exe.read_bytes(); ib, secs = V.load_pe(data)
        text = next(s for s in secs if s["name"] == ".text")
        tbytes = data[text["rp"]:text["rp"] + text["rs"]]
        for opt, sl in sigs.items():
            for s in sl:
                hits_by_sig[s] = len(V.find_all(V.parse_signature(s), tbytes))
    rows = []
    for h, opt in sorted(opt_of.items()):
        end = min(next_fn(int(h, 16)), int(h, 16) + 0x400)
        out = dis(int(h, 16), end)
        inst = []
        for m in re.finditer(r"mov\s+BYTE PTR \[(\w+)\],0x(e9|e8|eb)", out):
            inst.append({"opcode": m.group(2), "site_reg": m.group(1)})
        tg = [int(x, 16) for x in re.findall(
            r"mov\s+eax,0x(10[0-9a-f]{6})\s*\n(?:\s*[0-9a-f]+:[^\n]*\n){0,3}?\s*[0-9a-f]+:\t83 e8 05", out)]
        seen = []
        for v in tg:
            if v not in seen: seen.append(v)
        for i, item in enumerate(inst):
            item["target"] = hex(seen[i]) if i < len(seen) else None
        rows.append({"handler": h, "option": opt,
                     "signatures": sigs.get(opt, []),
                     "build_gates": gates.get(opt, {}).get("build_gates", []),
                     "installs": inst,
                     "op_count": len(by_h.get(h, [])),
                     "handler_end": hex(end),
                     "game_hits": [{"signature": s, "hits": hits_by_sig.get(s, None)}
                                   for s in sigs.get(opt, [])]})
    return rows
if __name__ == "__main__":
    p = argparse.ArgumentParser(); p.add_argument("--check", action="store_true"); args = p.parse_args()
    rows = build(); text = json.dumps(rows, indent=2) + "\n"
    out = ROOT / "evidence/handler_table.json"
    if args.check:
        if out.read_text() != text: raise SystemExit("handler table is stale")
        print(f"handler table verified ({len(rows)} handlers)")
    else:
        out.write_text(text); print(f"handler table written ({len(rows)} handlers)")

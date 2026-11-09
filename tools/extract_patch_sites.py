#!/usr/bin/env python3
"""Generate the recovered patch-site table from handler evidence (read-only)."""
import argparse, json, pathlib
ROOT = pathlib.Path(__file__).resolve().parents[1]
def build():
    signatures = json.loads((ROOT / "evidence/option_signatures.json").read_text())
    jumps = json.loads((ROOT / "evidence/option_jump_sites.json").read_text())
    by_option = {}
    for site in jumps:
        for option in site["option"]:
            by_option.setdefault(option, []).append(site)
    rows = []
    for option, sigs in signatures.items():
        site = (by_option.get(option) or [{}])[0]
        targets = []
        for target in site.get("targets", []):
            if target.startswith("0x"):
                targets.append(int(target, 16))
            elif "_" in target:
                targets.append(int(target.split("_")[1], 16))
        rows.append((option, sigs[0], targets[0] if targets else 0, len(targets)))
    rows.sort()
    lines = ["// Generated from ZolikaPatch handlers; signatures recovered read-only."]
    for option, signature, target, count in rows:
        lines.append(f'  {{"{option}", "{signature}", {target}u, {count}}},')
    return "\n".join(lines) + "\n"
if __name__ == "__main__":
    p = argparse.ArgumentParser(); p.add_argument("--check", action="store_true"); args = p.parse_args()
    output = ROOT / "src/recovered_patch_sites.inc"; text = build()
    if args.check:
        if output.read_text() != text: raise SystemExit("recovered patch sites are stale")
        print("recovered patch sites verified")
    else:
        output.write_text(text); print("recovered patch sites written")

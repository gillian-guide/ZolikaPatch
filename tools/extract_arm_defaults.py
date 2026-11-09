#!/usr/bin/env python3
"""Extract the per-option loader arm defaults (the string the original's
GetString-equivalent substitutes when the INI key is absent) and emit the table
the config layer uses for missing keys.

Source: the IDA decompilation of the loader `sub_100523C0`
(`ZolikaPatch_decompiled/ida/100523C0_sub_100523C0.c`), which prints every arm as
either
    sub_1005C160(v469, "Key", "default")            (plain key literal)
or the reversed-key form
    sub_10057870/40(v481, "yeK")                    (reversed literal builder)
    ...
    *(_DWORD *)v455 = "default";                    (default literal)
    v = sub_1005BFF0(...)                           (map get)
plus the one runtime-composed key (CarDynamicShadowFix via sub_10001280's
"Ca%snam%s%swF%s" + "rDy","icSh","ado","ix").

The emitted table is the missing-key fallback ONLY: the shipped INI already
carries every key, and its values (evidence/ini_records.json) are the "observed
defaults" in the feature catalog. The arm default can differ from the shipped
value; see docs/parity_and_improvements.md.
"""
import argparse, json, pathlib, re
ROOT = pathlib.Path(__file__).resolve().parents[1]
LOADER = ROOT.parent / "ZolikaPatch_decompiled/ida/100523C0_sub_100523C0.c"

FORM_A = re.compile(r'sub_1005C160\(\(int\)&?v\d+, \(int\)"([^"]+)", "([^"]*)"\)')
REV_KEY = re.compile(r'sub_100578(?:70|40)\(&?v\d+, "([^"]+)"\)')
DEFAULT = re.compile(r'\*\(_DWORD \*\)&?v455(?:\[4\])? = "([^"]*)"')
COMPOSED = re.compile(r'sub_10001280\(v\d+, 0x40u, "Ca%snam%s%swF%s", "rDy", "icSh", "ado", "ix"\)')
DIRECT_KEY = re.compile(r'sub_10058620\(&?v\d+(?:\[\d+\])?, "([^"]+)", (?:0x[0-9a-fA-F]+|\d+)u?\)')
DIRECT_DEFAULT = re.compile(r'sub_10058620\(&?v\d+(?:\[\d+\])?, "([^"]*)", (?:0x[0-9a-fA-F]+|\d+)u?\)')


def known_keys():
    refs = json.loads((ROOT / "evidence/option_references.json").read_text())
    return set(refs) | {"Patch7RemoveModChecks"}


def extract():
    text = LOADER.read_text(encoding="utf-8", errors="replace")
    keys = known_keys()
    defaults = {}
    for key, default in FORM_A.findall(text):
        if key in keys:
            defaults[key] = default
    for match in REV_KEY.finditer(text):
        key = match.group(1)[::-1]
        if key not in keys:
            continue
        found = DEFAULT.search(text, match.end(), match.end() + 1500)
        if not found:
            raise SystemExit(f"no default after reversed key {key}")
        defaults[key] = found.group(1)
    for match in DIRECT_KEY.finditer(text):
        key = match.group(1)
        if key not in keys:
            continue  # section names / default-string construction
        previous = None
        for candidate in DIRECT_DEFAULT.finditer(text, 0, match.start()):
            previous = candidate
        if previous is None or previous.group(1) not in ("0", "1"):
            raise SystemExit(f"no default before direct key {key}")
        defaults[key] = previous.group(1)
    # CarDynamicShadowFix: composed key; the map getter at 0x1005C270 pushes
    # the constant default "0" (0x1012b264) internally.
    if COMPOSED.search(text):
        defaults["CarDynamicShadowFix"] = "0"
    return defaults


def main():
    p = argparse.ArgumentParser(); p.add_argument("--check", action="store_true"); args = p.parse_args()
    defaults = extract()
    json_text = json.dumps(defaults, indent=2, sort_keys=True) + "\n"
    lines = ["// Generated from the loader arm defaults (missing-key fallback).",
             "// Fields: option, arm default string.",
             "// The shipped INI carries every key; these apply only when a key is absent."]
    for key in sorted(defaults):
        lines.append(f'  {{"{key}", "{defaults[key]}"}},')
    inc_text = "\n".join(lines) + "\n"
    json_out = ROOT / "evidence/option_arm_defaults.json"
    inc_out = ROOT / "src/recovered_arm_defaults.inc"
    if args.check:
        if json_out.read_text() != json_text or inc_out.read_text() != inc_text:
            raise SystemExit("arm defaults are stale")
        print("arm defaults verified")
    else:
        json_out.write_text(json_text); inc_out.write_text(inc_text)
        print(f"arm defaults written ({len(defaults)} keys)")


if __name__ == "__main__":
    main()

"""Insert a clean skip at the top of the two drift generators' main().

Both compare a generated file against an input that lives outside this
repository (a research note, the reference tree). In a clone those inputs are
absent, which is not drift: the check should say so and pass, rather than fail
with FileNotFoundError and make CI look broken.
"""

import pathlib

GUARDS = {
    "tools/gen_options.py": (
        "    if not INVENTORY.exists():\n"
        "        # The inventory is a research note kept outside this repository;\n"
        "        # without it there is nothing to compare, which is not drift.\n"
        '        print(f"skip: {INVENTORY} is absent, so the catalogue has nothing to be checked against")\n'
        "        return 0\n"
    ),
    "tools/gen_body_targets.py": (
        "    if not TABLE.exists():\n"
        "        # The reference tree is not part of this repository; without it the\n"
        "        # table cannot be regenerated, which is not drift.\n"
        '        print(f"skip: {TABLE} is absent, so the body table cannot be checked")\n'
        "        return 0\n"
    ),
}

for name, guard in GUARDS.items():
    path = pathlib.Path(name)
    text = path.read_text(encoding="utf-8")
    if "is absent, so" in text:
        print(f"{name}: already guarded")
        continue
    lines = text.splitlines(keepends=True)
    out, inserted = [], False
    for line in lines:
        out.append(line)
        if line.strip() == "def main() -> int:" and not inserted:
            out.append(guard)
            inserted = True
    if not inserted:
        raise SystemExit(f"{name}: main() not found")
    path.write_text("".join(out), encoding="utf-8")
    print(f"{name}: guarded")

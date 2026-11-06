#!/usr/bin/env python3
"""Guard the oracle-verified class.

The scoreboard reports these keys apart from the open ones so that a baseline known to
be incomplete does not read as unfinished work. That is only honest while the list
stays small, documented and real - so this check fails if an entry names a key that is
not in the catalogue, if an entry lacks its justification, documentation anchor or
measurement, or if the list grows past the two the project has evidence for.

    python tools/check_oracle_verified.py
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TSV = ROOT / "tests" / "oracle_verified.tsv"
CATALOGUE = ROOT / "source" / "options.inc"
LIMIT = 2


def catalogue():
    return {line.split("(", 1)[1].split(",", 1)[0]
            for line in CATALOGUE.read_text(encoding="utf-8").splitlines()
            if line.startswith("ZP_OPTION(")}


def main() -> int:
    if not TSV.exists():
        print("no oracle_verified.tsv: the scoreboard has no excused keys")
        return 0

    known = catalogue()
    failures = 0
    entries = 0
    for number, line in enumerate(TSV.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        entries += 1
        parts = [part.strip() for part in line.split("\t")]
        if len(parts) < 4 or not all(parts[:4]):
            print(f"line {number}: needs key, why, doc anchor and measurement")
            failures += 1
            continue
        key, why, anchor, measured = parts[:4]
        if key not in known:
            print(f"line {number}: {key} is not in the catalogue")
            failures += 1
        if not (ROOT / anchor.split("#", 1)[0]).exists():
            print(f"line {number}: {key} points at a document that does not exist")
            failures += 1
        if len(why) < 40 or len(measured) < 20:
            print(f"line {number}: {key} needs a real justification and measurement")
            failures += 1

    if entries > LIMIT:
        print(f"{entries} excused keys, but only {LIMIT} have evidence")
        failures += 1

    print(f"{entries} oracle-verified key(s), {failures} failure(s)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

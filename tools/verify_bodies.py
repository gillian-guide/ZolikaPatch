#!/usr/bin/env python3
"""Prove transcribed hook bodies against the original ASI.

A body is transcribed byte-for-byte from `ZolikaPatch_original.asi.off`, so the
candidate's bytes at the body's address must equal the original's bytes at its
recovered VA, once module-relative operands are accounted for. This compares
them and reports one line per body.

    python tools/verify_bodies.py --original <asi> --candidate <asi> --bodies <tsv>

The manifest is one row per body: `name <recovered VA> <candidate VA> <length>`,
both VAs as hexadecimal file offsets. `--selftest` runs the comparator against
the original itself (must pass) and against a one-byte corruption (must fail),
which is how the tool is checked before the first real body exists.

Exit status: 0 proved, 1 a body differs, 2 inputs missing.
"""
import argparse
import sys
from pathlib import Path

IMAGE_BASE = 0x10000000


def image_address_to_offset(va: int) -> int:
    return va - IMAGE_BASE


def compare(original: bytes, candidate: bytes, rows):
    results = []
    for name, original_va, candidate_va, length in rows:
        start_o = image_address_to_offset(original_va)
        start_c = image_address_to_offset(candidate_va)
        if start_o < 0 or start_c < 0 or start_o + length > len(original) or start_c + length > len(candidate):
            results.append((name, "MISSING", "outside the image"))
            continue
        want = original[start_o:start_o + length]
        have = candidate[start_c:start_c + length]
        if want == have:
            results.append((name, "PASS", ""))
        else:
            first = next(i for i in range(length) if want[i] != have[i])
            results.append((name, "FAIL", f"byte {first}: {want[first]:02X} != {have[first]:02X}"))
    return results


def read_manifest(path: Path):
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, original_va, candidate_va, length = line.split()
        rows.append((name, int(original_va, 16), int(candidate_va, 16), int(length, 0)))
    return rows


def report(results) -> int:
    for name, status, detail in results:
        print(f"{status} {name}" + (f" ({detail})" if detail else ""))
    failed = sum(1 for _, status, _ in results if status != "PASS")
    print(f"{len(results)} body(ies), {failed} failure(s)")
    return 0 if failed == 0 else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--original")
    parser.add_argument("--candidate")
    parser.add_argument("--bodies")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        if not args.original:
            raise SystemExit("--selftest needs --original")
        original = Path(args.original).read_bytes()
        rows = [("selftest-body", IMAGE_BASE + 0x1000, IMAGE_BASE + 0x1000, 32)]
        first = report(compare(original, original, rows))
        corrupted = bytearray(original)
        corrupted[0x1010] ^= 0xFF
        second = report(compare(original, bytes(corrupted), rows))
        print("selftest: identical bytes PASS, one corrupted byte FAIL"
              if first == 0 and second == 1 else "selftest: BROKEN")
        return 0 if first == 0 and second == 1 else 2

    if not (args.original and args.candidate and args.bodies):
        raise SystemExit(__doc__)
    return report(compare(Path(args.original).read_bytes(), Path(args.candidate).read_bytes(),
                          read_manifest(Path(args.bodies))))


if __name__ == "__main__":
    sys.exit(main())

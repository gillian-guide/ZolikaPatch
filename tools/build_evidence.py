#!/usr/bin/env python3
"""Build deterministic, non-binary evidence summaries from read-only inputs."""
from __future__ import annotations
import argparse, csv, datetime, hashlib, json, re, struct, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE.parent / "evidence"
ROOT = HERE.parent.parent
EXTRACTED = ROOT / "ZolikaPatch_extracted"
DECOMPILED = ROOT / "ZolikaPatch_decompiled" / "ghidra"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def ini_records(path: Path):
    section = ""
    records = []
    for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
        match = re.match(r"\s*\[([^]]+)\]\s*$", line)
        if match:
            section = match.group(1)
            continue
        if not line.lstrip().startswith(";") and "=" in line:
            key, value = line.split("=", 1)
            if key.strip():
                records.append({"line": number, "section": section,
                                "key": key.strip(), "value": value.strip()})
    return records


def function_summary(path: Path):
    with path.open(newline="", errors="replace") as stream:
        rows = list(csv.DictReader(stream))
    noise = ("Catch", "Unwind", "switchD", "FID_", "LAB_")
    for row in rows:
        name = row["name"]
        if row["thunk"] == "true": category = "import_thunk"
        elif name.startswith("FUN_"): category = "anonymous_candidate"
        elif name.startswith(noise): category = "exception_or_generated"
        else: category = "named_library_or_candidate"
        row["category"] = category
    counts = {}
    for row in rows:
        counts[row["category"]] = counts.get(row["category"], 0) + 1
    return rows, counts


def pe_facts(path: Path):
    """Normalized PE header facts, parsed natively so results are identical on
    every host (the previous objdump-based extraction varied by binutils build
    and local timezone)."""
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise RuntimeError(f"not a PE file: {path}")
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew : e_lfanew + 4] != b"PE\0\0":
        raise RuntimeError(f"missing PE signature: {path}")
    coff = e_lfanew + 4
    machine, _sections, timestamp = struct.unpack_from("<HHI", data, coff)
    optional = coff + 20
    magic = struct.unpack_from("<H", data, optional)[0]
    if magic != 0x10B:
        raise RuntimeError(f"unexpected optional-header magic {magic:#x}: {path}")
    entry = struct.unpack_from("<I", data, optional + 16)[0]
    image_base = struct.unpack_from("<I", data, optional + 28)[0]
    major_subsystem = struct.unpack_from("<H", data, optional + 48)[0]
    minor_subsystem = struct.unpack_from("<H", data, optional + 50)[0]
    size_of_image = struct.unpack_from("<I", data, optional + 56)[0]
    subsystem = struct.unpack_from("<H", data, optional + 68)[0]
    architecture = {0x14C: "i386", 0x8664: "x86-64"}.get(machine, f"machine {machine:#x}")
    built = datetime.datetime.fromtimestamp(timestamp, datetime.timezone.utc)
    return [
        f"architecture: {architecture}",
        f"address_of_entry_point: {entry:08x}",
        f"image_base: {image_base:08x}",
        f"major_subsystem_version: {major_subsystem}",
        f"minor_subsystem_version: {minor_subsystem}",
        f"size_of_image: {size_of_image:08x}",
        f"subsystem: {subsystem}",
        f"time_date: {built.strftime('%a %b %d %H:%M:%S %Y UTC')}",
    ]


def generate():
    files = sorted(
        (p for p in EXTRACTED.rglob("*") if p.is_file() and not p.name.startswith("._")),
        key=lambda p: p.relative_to(ROOT).as_posix(),
    )
    hashes = [{"path": p.relative_to(ROOT).as_posix(), "size": p.stat().st_size,
               "sha256": sha256(p)} for p in files]
    records = ini_records(EXTRACTED / "ZolikaPatch.ini")
    rows, categories = function_summary(DECOMPILED / "functions.csv")
    section_counts = {}
    for record in records:
        section_counts[record["section"]] = section_counts.get(record["section"], 0) + 1
    summary = {
        "schema": 1,
        "inputs": {"extracted": EXTRACTED.relative_to(ROOT).as_posix(),
                   "decompiled": DECOMPILED.relative_to(ROOT).as_posix()},
        "hashes": hashes,
        "pe": pe_facts(EXTRACTED / "ZolikaPatch.asi"),
        "ini": {"assignment_count": len(records), "section_counts": section_counts},
        "functions": {
            "count": len(rows),
            "ok": sum(r["status"] == "ok" for r in rows),
            "failed": [{"address": r["address"], "status": r["status"].strip()}
                       for r in rows if r["status"] != "ok"],
            "chunks": {str(i): sum(r["chunk"] == str(i) for r in rows) for i in range(5)},
            "categories": categories,
        },
    }
    return {
        "summary.json": json.dumps(summary, indent=2, sort_keys=True) + "\n",
        "ini_records.json": json.dumps(records, indent=2, sort_keys=True) + "\n",
        "functions_classified.csv": csv_text(rows),
    }


def csv_text(rows):
    import io
    stream = io.StringIO(newline="")
    writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator="\n")
    writer.writeheader(); writer.writerows(rows)
    return stream.getvalue()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    generated = generate()
    OUT.mkdir(parents=True, exist_ok=True)
    mismatches = []
    for name, content in generated.items():
        target = OUT / name
        if args.check:
            if not target.exists() or target.read_text() != content: mismatches.append(name)
        else:
            target.write_text(content)
    if mismatches:
        print("stale evidence: " + ", ".join(mismatches), file=sys.stderr); return 1
    print(("verified" if args.check else "generated") + f" {len(generated)} evidence files")
    return 0

if __name__ == "__main__": raise SystemExit(main())

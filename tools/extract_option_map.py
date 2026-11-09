#!/usr/bin/env python3
"""Map ZolikaPatch [Options] keys to referencing functions (read-only evidence)."""
import argparse, hashlib, json, pathlib, struct
ROOT = pathlib.Path(__file__).resolve().parents[1]
EVIDENCE_SHA = "720fbcebfd563616151560020eea12168115a9183d6813b432b0367d2b702fc5"

def sections(data):
    pe = struct.unpack_from("<I", data, 60)[0]
    opt = pe + 24
    image_base = struct.unpack_from("<I", data, opt + 28)[0]
    start = opt + struct.unpack_from("<H", data, pe + 20)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    return image_base, [struct.unpack_from("<8sIIIIIIHHI", data, start + 40 * i) for i in range(count)]

def build():
    data = (ROOT.parent / "ZolikaPatch_extracted/ZolikaPatch.asi").read_bytes()
    if hashlib.sha256(data).hexdigest() != EVIDENCE_SHA:
        raise ValueError("unrecognized evidence binary")
    image_base, secs = sections(data)
    def to_va(offset):
        for s in secs:
            if s[4] <= offset < s[4] + s[3]:
                return image_base + s[2] + (offset - s[4])
        return None
    text = next(s for s in secs if s[0].startswith(b".text"))
    functions = sorted(
        (int(row["address"], 16), row["entry_name"])
        for row in __import__("csv").DictReader((ROOT.parent / "ZolikaPatch_decompiled/ghidra/functions.csv").open()))
    records = json.loads((ROOT / "evidence/ini_records.json").read_text())
    result = {}
    for record in records:
        if record.get("section") != "Options":
            continue
        key = record["key"]
        # MSVC string pooling can store a literal reversed; accept forward,
        # reversed, and reversed-suffix-inside-a-larger-pooled-string forms.
        candidates = [data.find(key.encode() + b"\0")]
        reversed_offset = data.find(key[::-1].encode() + b"\0")
        if reversed_offset >= 0:
            # Pooled storage may append the reversed key to a neighbouring name.
            candidates.append(reversed_offset)
        offset = next((c for c in candidates if c >= 0), -1)
        va = to_va(offset) if offset >= 0 else None
        refs = []
        if va is not None:
            needle = struct.pack("<I", va)
            position = text[4]
            while True:
                found = data.find(needle, position, text[4] + text[3])
                if found < 0:
                    break
                refs.append(image_base + text[2] + (found - text[4]))
                position = found + 1
        owners = []
        for reference in refs:
            index = __import__("bisect").bisect_right([f[0] for f in functions], reference) - 1
            if index >= 0:
                owners.append(functions[index][1])
        result[key] = {"string_va": None if va is None else hex(va),
                       "refs": [hex(r) for r in refs],
                       "functions": sorted(set(owners))}
    return json.dumps(result, indent=2) + "\n"

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    output = ROOT / "evidence/option_references.json"
    text = build()
    if args.check:
        if output.read_text() != text:
            raise SystemExit("option map is stale")
        print("option map verified: 98 keys")
    else:
        output.write_text(text)

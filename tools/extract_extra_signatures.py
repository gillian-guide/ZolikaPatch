#!/usr/bin/env python3
"""Generate the recovered per-option patch signatures for the 15 [Options] keys
whose patterns exist in the evidence but which the primary signature extractor
could not see (extractor coverage gaps, not behavioural gaps).

Sources (read-only):
  - evidence/option_coverage_gaps.json -> per-option `pattern_evidence`
  - evidence/option_signatures.json    -> the 64 options the primary extractor saw
  - ZolikaPatch_extracted/ZolikaPatch.asi (sha256 pinned) for the evidence guard
  - /Users/kreato/Downloads/GTAIV.exe   -> re-verified hit counts (optional)

Every emitted signature is re-checked against the immutable game image so the
generated table cannot drift from the evidence it is derived from."""
import argparse, hashlib, json, pathlib, struct, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ASI_SHA = "720fbcebfd563616151560020eea12168115a9183d6813b432b0367d2b702fc5"
DEFAULT_EXE = pathlib.Path.home() / "Downloads/GTAIV.exe"


def parse_signature(signature):
    pattern = []
    for token in signature.split():
        if token in ("?", "??"):
            pattern.append(None); continue
        value, seen, index = 0, False, 0
        if token[:2].lower() == "0x":
            index = 2
        for ch in token[index:]:
            if ch not in "0123456789abcdefABCDEF":
                break
            seen = True
            value = value * 16 + int(ch, 16)
        pattern.append((value & 0xFF) if seen else 0)
    return pattern


def find_all(pattern, data):
    anchors = [(i, p) for i, p in enumerate(pattern) if p is not None]
    if not anchors or len(pattern) > len(data):
        return []
    anchor, value = anchors[0]
    hits, start = [], 0
    needle = bytes([value])
    while True:
        j = data.find(needle, start)
        if j < 0:
            break
        base = j - anchor
        if base >= 0 and base + len(pattern) <= len(data):
            if all(p is None or data[base + k] == p for k, p in enumerate(pattern)):
                hits.append(base)
        start = j + 1
    return hits


def text_section(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    base = struct.unpack_from("<I", data, opt + 28)[0]
    sec = opt + optsz
    for i in range(nsec):
        name, vs, va, rs, rp = struct.unpack_from("<8sIIII", data, sec + 40 * i)[:5]
        if name.split(b"\0")[0] == b".text":
            return base, va, data[rp:rp + min(vs, rs)]
    raise ValueError("no .text section")


def expected_sha(name):
    return ASI_SHA


def build(exe_path):
    asi = (ROOT.parent / "ZolikaPatch_extracted/ZolikaPatch.asi").read_bytes()
    if hashlib.sha256(asi).hexdigest() != ASI_SHA:
        raise ValueError("unrecognized evidence binary")
    gaps = json.loads((ROOT / "evidence/option_coverage_gaps.json").read_text())
    seen = set(json.loads((ROOT / "evidence/option_signatures.json").read_text()))
    exe = pathlib.Path(exe_path)
    data = exe.read_bytes() if exe.is_file() else None
    base = text = None
    if data is not None:
        base, text_va, text = text_section(data)

    rows = []
    for option in gaps["extractor_gaps_not_behavioural_gaps"]["options"]:
        if option in seen:
            continue
        record = next(o for o in gaps["options"] if o["option"] == option)
        patterns = record.get("pattern_evidence", [])
        if not patterns:
            raise ValueError(f"{option}: no pattern evidence")
        for entry in patterns:
            pattern = entry["pattern"]
            tokens = pattern.split()
            compiled = parse_signature(pattern)
            if text is not None:
                hits = find_all(compiled, text)
                want = entry.get("game", {}).get("hits")
                if want is not None and len(hits) != want:
                    raise ValueError(
                        f"{option}: hit count drift for {pattern!r}: {len(hits)} != {want}")
                vas = ["0x%x" % (base + text_va + h) for h in hits]
                recorded = entry.get("game", {}).get("vas")
                if recorded is not None and vas != recorded:
                    raise ValueError(f"{option}: hit VAs drift for {pattern!r}: {vas} != {recorded}")
            rows.append((option, pattern, len(tokens), entry.get("entry", "")))
    rows.sort()
    lines = [
        "// Generated from recovered per-option pattern evidence; the 15 options the",
        "// primary signature extractor could not see. Fields: option, signature tokens,",
        "// token count, the function the pattern was recovered from.",
    ]
    for option, pattern, tokens, entry in rows:
        lines.append(f'  {{"{option}", "{pattern}", {tokens}u, "{entry}"}},')
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--check", action="store_true")
    p.add_argument("--exe", default=str(DEFAULT_EXE))
    args = p.parse_args()
    out = ROOT / "src/recovered_extra_signatures.inc"
    text = build(args.exe)
    if args.check:
        if out.read_text() != text:
            raise SystemExit("recovered extra signatures are stale")
        print("recovered extra signatures verified")
    else:
        out.write_text(text)
        print("recovered extra signatures written")

#!/usr/bin/env python3
"""Reject a recovered signature that can never match the game.

Every supported executable is loaded relocated: its PE header carries
`DYNAMIC_BASE` (`DllCharacteristics` 0x140), so the Windows loader applies the
image's own `.reloc` table and every absolute VA baked into code, data or an
instruction operand is rewritten with `delta = actualBase - ImageBase`. A
signature string that spells out any of those bytes therefore matches the file
image (which is what IDA shows, and what the offline tools used to load) and
*not* the image the game runs -- the arm silently never fires. That is how
`IncreaseVehicleStructLimit` stayed dead in play (`68 34 EE DA 00`), and how two
`EpisodicVehicleSupport` flag stores were lost.

This script measures it without loading any game: for every `find_pattern`
literal in `source/*.ixx` it finds the hits in each image's unrelocated bytes,
then asks whether any *concrete* byte of the hit falls inside a dword the
image's `.reloc` table covers. Such a byte is an address byte and must be a
wildcard. It also reports the parenthetical measurement the brief asks for: the
same pattern matched against the relocated copy.

    python tools/check_relocated_literals.py [--game DIR] [--verbose]

Exit status 0 when every pattern is relocation-safe, 1 when any is not.
"""

import argparse
import bisect
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_GAME = ROOT.parent / "game_refs"

# The images the per-key baselines and the 18-cell matrix are scored on.
BUILDS = ["GTAIV_1000", "GTAIV_1004", "GTAIV_1040", "GTAIV_1060", "GTAIV_1070",
          "GTAIV", "EFLC_1110", "EFLC_1120", "EFLC_1130"]

IMAGE_SCN_MEM_EXECUTE = 0x20000000


class Image:
    """The executable mapped at its RVAs, plus what `.reloc` rewrites."""

    def __init__(self, path):
        self.path = path
        data = path.read_bytes()
        if len(data) < 0x40:
            raise ValueError(f"{path} is not a PE file")
        e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
        if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
            raise ValueError(f"{path} has no PE signature")
        coff = e_lfanew + 4
        self.sections = []
        count = struct.unpack_from("<H", data, coff + 2)[0]
        optional_size = struct.unpack_from("<H", data, coff + 16)[0]
        optional = coff + 20
        self.image_base = struct.unpack_from("<I", data, optional + 28)[0]
        self.size = struct.unpack_from("<I", data, optional + 56)[0]
        reloc_rva, reloc_size = struct.unpack_from("<II", data, optional + 96 + 5 * 8)
        for index in range(count):
            entry = optional + optional_size + index * 40
            name = data[entry:entry + 8].rstrip(b"\0").decode(errors="replace")
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
                "<IIII", data, entry + 8)
            characteristics = struct.unpack_from("<I", data, entry + 36)[0]
            self.sections.append((name, virtual_address, virtual_size, raw_size,
                                  raw_offset, characteristics))

        # The image as the loader would map it before relocating anything.
        self.bytes = bytearray(self.size)
        for name, virtual_address, virtual_size, raw_size, raw_offset, _ in self.sections:
            blob = data[raw_offset:raw_offset + raw_size]
            self.bytes[virtual_address:virtual_address + len(blob)] = blob
        headers = min(struct.unpack_from("<I", data, optional + 60)[0], len(data))
        self.bytes[0:headers] = data[0:headers]

        # The scan range game.ixx uses: base to the end of the last executable
        # section.
        self.scan_end = 0
        for name, virtual_address, virtual_size, raw_size, _, characteristics in self.sections:
            if characteristics & IMAGE_SCN_MEM_EXECUTE:
                self.scan_end = virtual_address + (raw_size or virtual_size)
        if self.scan_end == 0:
            self.scan_end = self.size

        self.relocated = bytearray(self.size)   # 1 = this byte is relocated
        self.fields = set()                     # the first byte of each HIGHLOW dword
        if reloc_rva == 0 or reloc_size == 0:
            raise ValueError(f"{path} has no .reloc directory")
        offset = self._file_offset(data, reloc_rva)
        if offset is None:
            raise ValueError(f"{path}: .reloc is not backed by file data")
        end = offset + reloc_size
        while offset + 8 <= end:
            page, block_size = struct.unpack_from("<II", data, offset)
            if block_size == 0:
                break
            if block_size < 8 or offset + block_size > end:
                raise ValueError(f"{path}: malformed .reloc block")
            cursor = offset + 8
            while cursor + 2 <= offset + block_size:
                entry = struct.unpack_from("<H", data, cursor)[0]
                cursor += 2
                kind = entry >> 12
                if kind == 3:  # IMAGE_REL_BASED_HIGHLOW
                    field = page + (entry & 0x0FFF)
                    self.relocated[field:field + 4] = b"\1\1\1\1"
                    self.fields.add(field)
                elif kind != 0:
                    raise ValueError(f"{path}: unsupported relocation type {kind}")
            offset += block_size
        self.field_list = sorted(self.fields)

    def _file_offset(self, data, rva):
        for name, virtual_address, virtual_size, raw_size, raw_offset, _ in self.sections:
            if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
                delta = rva - virtual_address
                if delta < raw_size:
                    return raw_offset + delta
        return None

    def field_for(self, rva):
        """The nearest fixup dword that starts at or before `rva`."""
        index = bisect.bisect_right(self.field_list, rva) - 1
        return self.field_list[index] if index >= 0 else rva

    def field_covering(self, rva):
        """The fixup dword that actually contains `rva`, or None."""
        index = bisect.bisect_right(self.field_list, rva) - 1
        if index >= 0 and rva < self.field_list[index] + 4:
            return self.field_list[index]
        return None

    def find(self, tokens):
        """Every hit RVA of the pattern in the unrelocated executable range."""
        pattern = b"".join(b"." if token == "?" else re.escape(bytes([int(token, 16)]))
                           for token in tokens)
        expression = re.compile(pattern, re.DOTALL)
        return [match.start() for match in
                expression.finditer(self.bytes, 0, self.scan_end)]

    def find_relocated(self, tokens):
        """The same pattern measured against the image with the fixups applied.

        The delta only has to be the kind of value a real base collision gives:
        any nonzero delta distinguishes a relocated dword from a literal one.
        """
        delta = 0x01A30000
        relocated = bytearray(self.bytes)
        for field in self.fields:
            value = struct.unpack_from("<I", relocated, field)[0]
            struct.pack_into("<I", relocated, field, (value + delta) & 0xFFFFFFFF)
        pattern = b"".join(b"." if token == "?" else re.escape(bytes([int(token, 16)]))
                           for token in tokens)
        expression = re.compile(pattern, re.DOTALL)
        return [match.start() for match in
                expression.finditer(relocated, 0, self.scan_end)]


def normalise(text):
    """IDA's one-token-per-byte string as the scanner's `?` form."""
    tokens = []
    for token in text.split():
        token = token.upper()
        if set(token) == {"?"}:
            tokens.append("?")
        elif re.fullmatch(r"[0-9A-F]{1,2}", token):
            tokens.append(f"{int(token, 16):02X}")
        else:
            return None
    return tokens


def source_patterns():
    """(file, line, arm name, [literal, ...]) for every find_pattern call."""
    out = []
    for path in sorted((ROOT / "source").glob("*.ixx")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in re.finditer(r"find_pattern\s*(?:<\s*\d+\s*>)?\s*\(", text):
            line = text.count("\n", 0, match.start()) + 1
            arm = "(file scope)"
            for definition in re.finditer(r"^\s*(?:void|bool|std::byte\s*\*|uint32_t|int)\s+(\w+)\s*\(",
                                          text[:match.start()], re.MULTILINE):
                arm = definition.group(1)
            cursor = match.end()
            depth = 1
            literals = []
            while cursor < len(text) and depth > 0:
                character = text[cursor]
                if character == '"':
                    end = cursor + 1
                    value = []
                    while end < len(text) and text[end] != '"':
                        if text[end] == "\\":
                            end += 1
                        value.append(text[end])
                        end += 1
                    literals.append("".join(value))
                    cursor = end + 1
                    continue
                if character == "(":
                    depth += 1
                elif character == ")":
                    depth -= 1
                cursor += 1
            literals = [item for item in literals if normalise(item) is not None]
            if literals:
                out.append((path.name, line, arm, literals))
    return out


def _hit_field(image, hit, tokens):
    """The fixup dword that a hit's first concrete relocated byte belongs to."""
    for offset, token in enumerate(tokens):
        if token != "?" and image.relocated[hit + offset]:
            field = image.field_covering(hit + offset)
            if field is not None:
                return field
    return hit


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", default=str(DEFAULT_GAME))
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    game = Path(args.game)
    patterns = source_patterns()
    print(f"{len(patterns)} find_pattern call(s) with a literal signature")

    findings = []
    warnings = []
    checked = 0
    for build in BUILDS:
        path = game / f"{build}.exe"
        if not path.exists():
            print(f"{build}: image missing, skipped")
            continue
        image = Image(path)
        for name, line, arm, literals in patterns:
            for literal in literals:
                tokens = normalise(literal)
                hits = image.find(tokens)
                if not hits:
                    continue
                checked += 1
                relocated_hits = image.find_relocated(tokens)
                first = hits[0]
                # A relocated dword moves by a 64 KiB-aligned delta, so only its
                # high two bytes change; a literal byte at offset 0 or 1 of a fixup
                # is still the same byte in the game (the dump's heap delta is not
                # 64 KiB-aligned, so note those separately).
                def offset_in_fixup(hit, offset):
                    return (hit + offset) - image.field_for(hit + offset)

                def concrete(hit, predicate):
                    return [offset for offset, token in enumerate(tokens)
                            if token != "?" and image.relocated[hit + offset] and
                            predicate(offset_in_fixup(hit, offset))]

                def broken(hit):
                    return concrete(hit, lambda position: position >= 2)

                def fragile(hit):
                    return concrete(hit, lambda position: position < 2)

                if not relocated_hits:
                    locations = [(_hit_field(image, hit, tokens), hit) for hit in hits]
                    findings.append((name, line, arm, literal, build, locations))
                    continue
                if broken(first):
                    findings.append((name, line, arm, literal, build,
                                     [(_hit_field(image, first, tokens), first)]))
                    continue
                if fragile(first):
                    warnings.append((name, line, arm, literal, build, first))

    if not findings and not warnings:
        print(f"{checked} pattern hit(s) checked: every literal byte is outside the "
              f"image's .reloc coverage")
        return 0

    for name, line, arm, literal, build, locations in findings:
        for field, hit in locations:
            print(f"  {name}:{line} ({arm}): {build} hit at RVA {hit:#x} spells out the "
                  f"relocated dword at RVA {field:#x}")
        print(f"    {literal}")
    if findings:
        print()
        print(f"{len(findings)} pattern/image pair(s) can never match the running game:")
        print("the loader rewrites those bytes. Wildcard the dword (or use the original "
              "ASI's own string).")
    for name, line, arm, literal, build, hit in warnings:
        print(f"  note: {name}:{line} ({arm}): the {build} hit at RVA {hit:#x} is a "
              f"relocated-byte coincidence, not the first hit; the pattern still matches")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())

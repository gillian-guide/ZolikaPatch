#!/usr/bin/env python3
"""Reference implementation of OutOfCommissionFix's decoder (the original ASI's
sub_1004B150/sub_1004B570), used by tools/extract_ooc_pak.py.

Decodes the embedded blob at VA 0x100B6798 into 179536 bytes; the OOC Adler-32
trailer check proves the transcription (see extract_ooc_pak.py).
"""
from __future__ import annotations

import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
ASI = ROOT / "game_refs" / "ZolikaPatch_original.asi.off"
IMAGE_BASE = 0x10000000
STREAM_VA = 0x100B6798
BLOB_BASE_VA = 0x100B6788
DEST_SIZE = 179536


def va_to_off(va: int) -> tuple[bytes, int]:
    data = ASI.read_bytes()
    pe = int.from_bytes(data[0x3C:0x40], "little")
    coff = pe + 4
    nsec = int.from_bytes(data[coff + 2 : coff + 4], "little")
    opt_size = int.from_bytes(data[coff + 16 : coff + 18], "little")
    opt = coff + 20
    sec_off = opt + opt_size
    for i in range(nsec):
        b = sec_off + i * 40
        vsize = int.from_bytes(data[b + 8 : b + 12], "little")
        sva = int.from_bytes(data[b + 12 : b + 16], "little")
        rawptr = int.from_bytes(data[b + 20 : b + 24], "little")
        if sva <= va - IMAGE_BASE < sva + vsize:
            return data, rawptr + (va - IMAGE_BASE - sva)
    raise SystemExit("VA not mapped")


class Decoder:
    """One-op-at-a-time decode of the original's custom LZ stream."""

    def __init__(self, stream: bytes, blob_base_off: int):
        self.src = stream
        self.src_base = blob_base_off
        self.dest = bytearray()
        self.overflow = False

    def decode(self, pc: int) -> int:
        src = self.src
        op = src[pc]
        dest = self.dest
        if op < 0x20:
            if op >= 0x18:
                count = src[pc + 3] + 1
                distance = src[pc + 2] + 256 * src[pc + 1] + 65536 * op - 1572863
                self._copy_backref(distance, count)
                return pc + 4
            if op >= 0x10:
                count = (src[pc + 3] << 8) + src[pc + 4] + 1
                distance = src[pc + 2] + 256 * src[pc + 1] + 65536 * op - 0xFFFFF
                self._copy_backref(distance, count)
                return pc + 5
            if op >= 8:
                count = 256 * op + src[pc + 1] - 2047
                self._copy_literal(pc + 2, count)
                return pc + 2 + count
            if op == 7:
                count = 256 * src[pc + 1] + src[pc + 2] + 1
                self._copy_literal(pc + 3, count)
                return pc + 3 + count
            if op == 6:
                count = src[pc + 4] + 1
                distance = src[pc + 3] + 256 * src[pc + 2] + 65536 * src[pc + 1] + 1
                self._copy_backref(distance, count)
                return pc + 5
            if op == 4:
                count = (src[pc + 4] << 8) + src[pc + 5] + 1
                distance = src[pc + 3] + 256 * src[pc + 2] + 65536 * src[pc + 1] + 1
                self._copy_backref(distance, count)
                return pc + 6
            return pc
        if op < 0x80:
            if op >= 0x40:
                count = src[pc + 2] + 1
                distance = src[pc + 1] + 256 * op - 0x3FFF
                self._copy_backref(distance, count)
                return pc + 3
            count = op - 31
            self._copy_literal(pc + 1, count)
            return pc + (op - 30)
        self._copy_backref(src[pc + 1] + 1, op - 127)
        return pc + 2

    def _copy_literal(self, from_off: int, count: int) -> None:
        if len(self.dest) + count <= DEST_SIZE:
            self.dest += self.src[from_off : from_off + count]
        else:
            self.overflow = True

    def _copy_backref(self, distance: int, count: int) -> None:
        if len(self.dest) + count > DEST_SIZE or distance > len(self.dest):
            self.overflow = True
            return
        for _ in range(count):
            self.dest.append(self.dest[-distance])


def main() -> int:
    data, stream_off = va_to_off(STREAM_VA)
    decoder = Decoder(data[stream_off : stream_off + 0x40000], 0)
    pc = 0
    while True:
        nxt = decoder.decode(pc)
        if nxt == pc:
            break
        pc = nxt
    print(f"dest={len(decoder.dest)} pc={pc:#x} overflow={decoder.overflow}")
    return 0 if len(decoder.dest) == DEST_SIZE else 1


if __name__ == "__main__":
    sys.exit(main())

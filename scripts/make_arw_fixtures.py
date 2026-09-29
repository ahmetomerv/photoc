#!/usr/bin/env python3
"""Generate original, tiny classic-TIFF ARW metadata fixtures (no sensor data).

All fixture bytes are authored by this project and covered by its MIT license.
These test metadata extraction, not camera RAW decoding or authenticity.
"""
import argparse
from pathlib import Path
import struct


def make_arw(endian="<", missing=False, exif_dimensions=True, raw_dimensions=True,
             maker=b"SONY\0", orientation=6, dng=False):
    def short(value):
        return struct.pack(endian + "H", value)

    def long(value):
        return struct.pack(endian + "I", value)

    def rational(numerator, denominator):
        return struct.pack(endian + "II", numerator, denominator)

    # Each entry: (tag, TIFF type, count, bytes or linked directory name).
    root = []
    if maker is not None:
        root.append((0x010f, 2, len(maker), maker))
    if not missing:
        root += [(0x0100, 4, 1, long(160)), (0x0101, 4, 1, long(120)),
                 (0x0106, 3, 1, short(2)),
                 (0x0110, 2, 13, b"DSC-RX100M7A\0"),
                 (0x0112, 3, 1, short(orientation)),
                 (0x8769, 4, 1, "exif"), (0x8825, 4, 1, "gps")]
        # Model count is derived from its actual bytes below.
        root = [(tag, kind, len(value) if tag == 0x0110 else count, value)
                for tag, kind, count, value in root]
        if raw_dimensions:
            root.append((0x014a, 4, 1, "raw"))
    if dng:
        root.append((0xc612, 1, 4, b"\1\4\0\0"))
    directories = {"root": root}
    if not missing:
        timestamp = b"2026:09:29 12:34:56\0"
        exif = [(0x829a, 5, 1, rational(1, 200)), (0x829d, 5, 1, rational(4, 1)),
                (0x8827, 3, 1, short(1600)), (0x9003, 2, len(timestamp), timestamp),
                (0x920a, 5, 1, rational(50, 1))]
        if exif_dimensions:
            exif += [(0xa002, 4, 1, long(5472)), (0xa003, 4, 1, long(3648))]
        directories["exif"] = exif
        directories["gps"] = [(1, 2, 2, b"S\0"), (2, 5, 3, rational(37, 1) + rational(48, 1) + rational(30, 1)),
                              (3, 2, 2, b"W\0"), (4, 5, 3, rational(122, 1) + rational(24, 1) + rational(15, 1))]
        if raw_dimensions:
            directories["raw"] = [(0x00fe, 4, 1, long(0)), (0x0100, 4, 1, long(5504)),
                                  (0x0101, 4, 1, long(3664)), (0x0106, 3, 1, short(32803))]
    offsets = {}
    position = 8
    for name, entries in directories.items():
        offsets[name] = position
        position += 2 + 12 * len(entries) + 4
    payload = bytearray()
    tables = bytearray()
    for entries in directories.values():
        tables += short(len(entries))
        for tag, kind, count, value in sorted(entries):
            if isinstance(value, str):
                value = long(offsets[value])
            if len(value) <= 4:
                storage = value.ljust(4, b"\0")
            else:
                if (position + len(payload)) % 2:
                    payload += b"\0"
                storage = long(position + len(payload))
                payload += value
            tables += struct.pack(endian + "HHI", tag, kind, count) + storage
        tables += b"\0" * 4
    signature = b"II" if endian == "<" else b"MM"
    return signature + short(42) + long(8) + tables + payload


def generate(destination):
    destination.mkdir(parents=True, exist_ok=True)
    files = {"sony-le.ARW": make_arw(), "sony-be.arw": make_arw(endian=">"),
             "missing.arw": make_arw(missing=True),
             "raw-dimensions.arw": make_arw(exif_dimensions=False)}
    for name, data in files.items():
        (destination / name).write_bytes(data)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "tests/fixtures/arw")
    generate(parser.parse_args().output)

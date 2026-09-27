"""Regenerate JPEG EXIF fixtures from the checked-in no_exif.jpg base.

Uses only the Python standard library. The small base image is an actual JPEG.
"""

from pathlib import Path
import struct
import zlib


ROOT = Path(__file__).resolve().parents[1] / "tests" / "fixtures" / "jpeg"
BASE = (ROOT / "no_exif.jpg").read_bytes()
assert BASE.startswith(b"\xff\xd8") and BASE.endswith(b"\xff\xd9")


def ascii_value(text):
    return 2, len(text) + 1, text.encode("ascii") + b"\0"


def short(value):
    return 3, 1, struct.pack("<H", value)


def long(value):
    return 4, 1, struct.pack("<I", value)


def rational(*pairs):
    return 5, len(pairs), b"".join(struct.pack("<II", *pair) for pair in pairs)


def make_tiff(with_gps):
    tiff = bytearray(b"II\x2a\x00\x00\x00\x00\x00")

    def add_ifd(tags):
        position = len(tiff)
        tiff.extend(struct.pack("<H", len(tags)))
        for tag, (kind, count, value) in sorted(tags.items()):
            tiff.extend(struct.pack("<HHI", tag, kind, count))
            if len(value) <= 4:
                tiff.extend(value.ljust(4, b"\0"))
            else:
                offset = len(tiff)
                tiff.extend(b"\0" * 4)
                pending.append((offset, value))
        tiff.extend(b"\0" * 4)
        return position

    pending = []
    exif_offset = add_ifd({
        0x829A: rational((1, 125)),
        0x829D: rational((28, 10)),
        0x8827: short(200),
        0x9003: ascii_value("2026:09:27 12:34:56"),
        0x920A: rational((50, 1)),
    })
    gps_offset = None
    if with_gps:
        gps_offset = add_ifd({
            0x0001: ascii_value("S"),
            0x0002: rational((37, 1), (48, 1), (30, 1)),
            0x0003: ascii_value("W"),
            0x0004: rational((122, 1), (24, 1), (15, 1)),
        })
    zero_tags = {
        0x010F: ascii_value("Fixture Camera Co."),
        0x0110: ascii_value("Model Z"),
        0x8769: long(exif_offset),
    }
    if gps_offset is not None:
        zero_tags[0x8825] = long(gps_offset)
    zero_offset = add_ifd(zero_tags)
    struct.pack_into("<I", tiff, 4, zero_offset)

    for offset, value in pending:
        while len(tiff) % 4:
            tiff.append(0)
        struct.pack_into("<I", tiff, offset, len(tiff))
        tiff.extend(value)
    return bytes(tiff)


for name, gps in (("with_exif.jpg", False), ("with_gps.jpeg", True)):
    payload = b"Exif\0\0" + make_tiff(gps)
    segment = b"\xff\xe1" + struct.pack(">H", len(payload) + 2) + payload
    (ROOT / name).write_bytes(BASE[:2] + segment + BASE[2:])

(ROOT / "invalid.jpg").write_bytes(b"\xff\xd8\xff\xe1\x00\x10truncated")


def png_chunk(kind, payload):
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload)))


png = b"\x89PNG\r\n\x1a\n"
png += png_chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0))
png += png_chunk(b"IDAT", zlib.compress(b"\0\xff\0\0"))
png += png_chunk(b"IEND", b"")
(ROOT / "unsupported.png").write_bytes(png)

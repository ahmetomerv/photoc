#!/usr/bin/env python3
"""Regenerate synthetic JPEG/PNG metadata fixtures under tests/fixtures/jpeg/.

All pixel data and EXIF values are invented for tests. No camera originals or
third-party photos are used. Requires only the Python standard library.

When `cjpeg` (libjpeg-turbo) is on PATH, the no-EXIF base is rebuilt as a 3x2
RGB JPEG. Otherwise the script writes a checked-in-equivalent byte sequence so
contributors can still refresh EXIF variants without installing cjpeg.

Usage:
  python3 scripts/make-metadata-fixtures.py
"""

from __future__ import annotations

from pathlib import Path
import shutil
import struct
import subprocess
import zlib


ROOT = Path(__file__).resolve().parents[1] / "tests" / "fixtures" / "jpeg"

# Fallback 3x2 RGB baseline JPEG (JFIF) produced by cjpeg -quality 90 -optimize
# from a synthetic PPM. Kept so EXIF variants can be rebuilt without cjpeg and
# without any external photograph.
NO_EXIF_FALLBACK = bytes.fromhex(
    "ffd8ffe000104a46494600010100000100010000ffdb00430003020203020203"
    "03030304030304050805050404050a070706080c0a0c0c0b0a0b0b0d0e12100d"
    "0e110e0b0b1016101113141515150c0f171816141812141514ffdb0043010304"
    "0405040509050509140d0b0d1414141414141414141414141414141414141414"
    "141414141414141414141414141414141414141414141414141414141414ffc0"
    "0011080002000303012200021101031101ffc400150001010000000000000000"
    "0000000000000005ffc4001e1000020202020300000000000000000000010200"
    "0305060407122141ffc4001501010100000000000000000000000000000708ff"
    "c4002011000003090100000000000000000000000002050103040635367274b2"
    "b3ffda000c03010002110311003f0091df9a46b95f6fecb5a60316b5d7725688"
    "387580aab5a05503c7d0000007c022225cb245ac95aee7cca175229d0d813960"
    "ffd9"
)


def ascii_value(text: str):
    return 2, len(text) + 1, text.encode("ascii") + b"\0"


def short(value: int):
    return 3, 1, struct.pack("<H", value)


def long(value: int):
    return 4, 1, struct.pack("<I", value)


def rational(*pairs):
    return 5, len(pairs), b"".join(struct.pack("<II", *pair) for pair in pairs)


def make_tiff(with_gps, model, iso, aperture, focal_length, captured):
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
        0x829D: rational(aperture),
        0x8827: short(iso),
        0x9003: ascii_value(captured),
        0x920A: rational(focal_length),
    })
    gps_offset = None
    if with_gps:
        # Invented coordinates near San Francisco Bay; not a real capture.
        gps_offset = add_ifd({
            0x0001: ascii_value("S"),
            0x0002: rational((37, 1), (48, 1), (30, 1)),
            0x0003: ascii_value("W"),
            0x0004: rational((122, 1), (24, 1), (15, 1)),
        })
    zero_tags = {
        0x010F: ascii_value("Fixture Camera Co."),
        0x0110: ascii_value(model),
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


def write_no_exif() -> bytes:
    """Return a tiny 3x2 RGB JPEG with no APP1/EXIF segment."""
    cjpeg = shutil.which("cjpeg")
    if cjpeg is not None:
        # Distinct RGB pixels so compression still yields a valid image.
        pixels = bytes([
            255, 0, 0, 0, 255, 0, 0, 0, 255,
            255, 255, 0, 0, 255, 255, 255, 0, 255,
        ])
        ppm = b"P6\n3 2\n255\n" + pixels
        encoded = subprocess.run(
            [cjpeg, "-quality", "90", "-optimize"],
            input=ppm,
            capture_output=True,
            check=True,
        ).stdout
        if encoded.startswith(b"\xff\xd8") and encoded.endswith(b"\xff\xd9"):
            (ROOT / "no_exif.jpg").write_bytes(encoded)
            return encoded
    (ROOT / "no_exif.jpg").write_bytes(NO_EXIF_FALLBACK)
    return NO_EXIF_FALLBACK


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))


def main() -> None:
    ROOT.mkdir(parents=True, exist_ok=True)
    base = write_no_exif()
    assert base.startswith(b"\xff\xd8") and base.endswith(b"\xff\xd9")

    for name, gps, model, iso, aperture, focal, captured in (
        ("with_exif.jpg", False, "Model Z", 200, (28, 10), (50, 1),
         "2026:09:27 12:34:56"),
        ("with_gps.jpeg", True, "Model Z", 200, (28, 10), (50, 1),
         "2026:09:27 12:34:56"),
        ("with_alt_exif.jpg", False, "Model A", 100, (4, 1), (35, 1),
         "2024:01:02 03:04:05"),
        ("session_1000.jpg", False, "Model S", 100, (4, 1), (35, 1),
         "2026:09:27 10:00:00"),
        ("session_1030.jpg", False, "Model S", 100, (4, 1), (35, 1),
         "2026:09:27 10:30:00"),
        ("session_1200.jpg", False, "Model S", 100, (4, 1), (35, 1),
         "2026:09:27 12:00:00"),
    ):
        payload = b"Exif\0\0" + make_tiff(gps, model, iso, aperture, focal,
                                          captured)
        segment = b"\xff\xe1" + struct.pack(">H", len(payload) + 2) + payload
        (ROOT / name).write_bytes(base[:2] + segment + base[2:])

    # Truncated SOI + APP1 header: enough to look like JPEG, not enough to parse.
    (ROOT / "invalid.jpg").write_bytes(b"\xff\xd8\xff\xe1\x00\x10truncated")

    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0))
    png += png_chunk(b"IDAT", zlib.compress(b"\0\xff\0\0"))
    png += png_chunk(b"IEND", b"")
    (ROOT / "unsupported.png").write_bytes(png)
    print(f"wrote metadata fixtures under {ROOT}")


if __name__ == "__main__":
    main()

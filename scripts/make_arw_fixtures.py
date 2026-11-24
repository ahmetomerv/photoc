#!/usr/bin/env python3
"""Generate original, tiny classic-TIFF Sony ARW fixtures.

All fixture bytes are authored by this project and covered by its MIT license.
Optional embedded JPEGs are copies of this project's own JPEG fixtures, and
optional sensor strips are arithmetic gradients. These test container,
preview, and metadata handling, not camera RAW decoding or authenticity.
"""
import argparse
from pathlib import Path
import struct


class Blob:
    """Bytes appended after all metadata; the entry stores their offset.
    Sensor strips go last, as in camera files."""

    def __init__(self, data, last=False):
        self.data = data
        self.last = last


def sensor_strip(width, height, endian="<"):
    """Synthetic 14-bit RGGB samples, one 16-bit word each."""
    samples = bytearray()
    for y in range(height):
        for x in range(width):
            value = 512 + (x * 9000 // width + y * 4000 // height)
            samples += struct.pack(endian + "H", value)
    return bytes(samples)


def make_arw(endian="<", missing=False, exif_dimensions=True, raw_dimensions=True,
             maker=b"SONY\0", orientation=6, dng=False, preview=None, thumbnail=None,
             sensor=None, sensor_bytes=None, root_extra=(), exif_extra=(), gps=True):
    """Optional parts:

    preview: JPEG bytes referenced from IFD0 (JPEGInterchangeFormat/Length).
    thumbnail: JPEG bytes referenced from IFD1, linked as IFD0's next directory.
    sensor: (width, height) uncompressed 14-bit CFA strip in the RAW SubIFD;
        sensor_bytes overrides the strip length recorded in StripByteCounts.
    root_extra/exif_extra: extra (tag, type, count, bytes) entries.
    """
    def short(value):
        return struct.pack(endian + "H", value)

    def long(value):
        return struct.pack(endian + "I", value)

    def rational(numerator, denominator):
        return struct.pack(endian + "II", numerator, denominator)

    # Each entry: (tag, TIFF type, count, bytes, linked directory name, or Blob).
    root = []
    if maker is not None:
        root.append((0x010f, 2, len(maker), maker))
    if not missing:
        root += [(0x0100, 4, 1, long(160)), (0x0101, 4, 1, long(120)),
                 (0x0106, 3, 1, short(2)),
                 (0x0110, 2, 13, b"DSC-RX100M7A\0"),
                 (0x0112, 3, 1, short(orientation)),
                 (0x8769, 4, 1, "exif")]
        if gps:
            root.append((0x8825, 4, 1, "gps"))
        # Model count is derived from its actual bytes below.
        root = [(tag, kind, len(value) if tag == 0x0110 else count, value)
                for tag, kind, count, value in root]
        if raw_dimensions or sensor is not None:
            root.append((0x014a, 4, 1, "raw"))
    if dng:
        root.append((0xc612, 1, 4, b"\1\4\0\0"))
    if preview is not None:
        root += [(0x0201, 4, 1, Blob(preview)), (0x0202, 4, 1, long(len(preview)))]
    root += list(root_extra)
    directories = {"root": root}
    if not missing:
        timestamp = b"2026:09:29 12:34:56\0"
        exif = [(0x829a, 5, 1, rational(1, 200)), (0x829d, 5, 1, rational(4, 1)),
                (0x8827, 3, 1, short(1600)), (0x9003, 2, len(timestamp), timestamp),
                (0x920a, 5, 1, rational(50, 1))]
        if exif_dimensions:
            exif += [(0xa002, 4, 1, long(5472)), (0xa003, 4, 1, long(3648))]
        directories["exif"] = exif + list(exif_extra)
        if gps:
            directories["gps"] = [(1, 2, 2, b"S\0"), (2, 5, 3, rational(37, 1) + rational(48, 1) + rational(30, 1)),
                                  (3, 2, 2, b"W\0"), (4, 5, 3, rational(122, 1) + rational(24, 1) + rational(15, 1))]
        if sensor is not None:
            width, height = sensor
            strip = sensor_strip(width, height, endian)
            count = len(strip) if sensor_bytes is None else sensor_bytes
            directories["raw"] = [(0x00fe, 4, 1, long(0)), (0x0100, 4, 1, long(width)),
                                  (0x0101, 4, 1, long(height)), (0x0102, 3, 1, short(14)),
                                  (0x0103, 3, 1, short(1)), (0x0106, 3, 1, short(32803)),
                                  (0x0111, 4, 1, Blob(strip, last=True)), (0x0115, 3, 1, short(1)),
                                  (0x0116, 4, 1, long(height)), (0x0117, 4, 1, long(count))]
        elif raw_dimensions:
            directories["raw"] = [(0x00fe, 4, 1, long(0)), (0x0100, 4, 1, long(5504)),
                                  (0x0101, 4, 1, long(3664)), (0x0106, 3, 1, short(32803))]
    if thumbnail is not None:
        directories["ifd1"] = [(0x00fe, 4, 1, long(1)), (0x0103, 3, 1, short(6)),
                               (0x0201, 4, 1, Blob(thumbnail)),
                               (0x0202, 4, 1, long(len(thumbnail)))]
    offsets = {}
    position = 8
    for name, entries in directories.items():
        offsets[name] = position
        position += 2 + 12 * len(entries) + 4
    payload = bytearray()
    tables = bytearray()
    blobs = []  # (table byte index of the offset field, data)
    for name, entries in directories.items():
        tables += short(len(entries))
        for tag, kind, count, value in sorted(entries, key=lambda entry: entry[0]):
            if isinstance(value, Blob):
                blobs.append((value.last, len(tables) + 8, value.data))
                value = long(0)
            elif isinstance(value, str):
                value = long(offsets[value])
            if len(value) <= 4:
                storage = value.ljust(4, b"\0")
            else:
                if (position + len(payload)) % 2:
                    payload += b"\0"
                storage = long(position + len(payload))
                payload += value
            tables += struct.pack(endian + "HHI", tag, kind, count) + storage
        tables += long(offsets["ifd1"]) if name == "root" and "ifd1" in offsets else b"\0" * 4
    signature = b"II" if endian == "<" else b"MM"
    data = bytearray(signature + short(42) + long(8) + tables + payload)
    for _, index, blob in sorted(blobs, key=lambda item: item[0]):
        if len(data) % 2:
            data += b"\0"
        data[8 + index:8 + index + 4] = long(len(data))
        data += blob
    return bytes(data)


def generate(destination):
    destination.mkdir(parents=True, exist_ok=True)
    jpeg = Path(__file__).resolve().parents[1] / "tests/fixtures/jpeg"
    files = {"sony-le.ARW": make_arw(), "sony-be.arw": make_arw(endian=">"),
             "missing.arw": make_arw(missing=True),
             "raw-dimensions.arw": make_arw(exif_dimensions=False),
             # The larger embedded JPEG (blurred.jpg) is the preview.
             "preview.ARW": make_arw(preview=(jpeg / "blurred.jpg").read_bytes(),
                                     thumbnail=(jpeg / "sharp.jpg").read_bytes(),
                                     sensor=(64, 48))}
    for name, data in files.items():
        (destination / name).write_bytes(data)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "tests/fixtures/arw")
    generate(parser.parse_args().output)

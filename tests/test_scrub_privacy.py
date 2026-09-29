#!/usr/bin/env python3
"""End-to-end privacy and all-metadata scrub regression fixtures."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BINARY = os.environ["PHOTOC_SCRUB_PRIVACY_BINARY"]
FIX = ROOT / "tests/fixtures/jpeg_metadata"
BASE = (FIX / "no_metadata.jpg").read_bytes()
EXIF = b"Exif\0\0"
XMP = b"http://ns.adobe.com/xap/1.0/\0"
ICC = b"ICC_PROFILE\0"


def marker(kind, payload):
    return bytes((255, kind)) + struct.pack(">H", len(payload) + 2) + payload


def segments(data):
    at = 2
    while at < len(data) and data[at:at + 2] != b"\xff\xda":
        assert data[at] == 255
        size = int.from_bytes(data[at + 2:at + 4], "big")
        yield data[at + 1], data[at + 4:at + size + 2]
        at += size + 2


def exif_fixture():
    # Build IFDs with distinct location, person, serial and orientation tags.
    tiff = bytearray(b"II\x2a\0\x08\0\0\0")
    root_entries = []
    exif_entries = []
    gps_entries = []

    def text_entry(tag, value):
        offset = len(tiff)
        blob = value.encode() + b"\0"
        tiff.extend(blob)
        return struct.pack("<HHII", tag, 2, len(blob), offset)

    root_entries.append(text_entry(0x013B, "Synthetic Artist"))
    exif_entries.extend((text_entry(0xA430, "Synthetic Owner"),
                         text_entry(0xA431, "BODY-12345"),
                         text_entry(0xA435, "LENS-12345"),
                         text_entry(0xA420, "UNIQUE-12345")))
    maker = b"opaque private maker bytes"
    offset = len(tiff)
    tiff.extend(maker)
    exif_entries.append(struct.pack("<HHII", 0x927C, 7, len(maker), offset))
    exif_at = len(tiff)
    tiff.extend(struct.pack("<H", len(exif_entries)) +
                b"".join(sorted(exif_entries)) + b"\0" * 4)
    gps_entries.append(struct.pack("<HHI4s", 0, 1, 4, b"\x02\x03\0\0"))
    gps_at = len(tiff)
    tiff.extend(struct.pack("<H", len(gps_entries)) +
                b"".join(gps_entries) + b"\0" * 4)
    root_entries.extend((struct.pack("<HHIHH", 0x0112, 3, 1, 6, 0),
                         struct.pack("<HHII", 0x8769, 4, 1, exif_at),
                         struct.pack("<HHII", 0x8825, 4, 1, gps_at)))
    root_at = len(tiff)
    tiff.extend(struct.pack("<H", len(root_entries)) +
                b"".join(sorted(root_entries)) + b"\0" * 4)
    struct.pack_into("<I", tiff, 4, root_at)
    return EXIF + tiff


def xmp_fixture():
    xml = (b'<x:xmpmeta xmlns:x="adobe:ns:meta/" '
           b'xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#" '
           b'xmlns:exif="http://ns.adobe.com/exif/1.0/" '
           b'xmlns:aux="http://ns.adobe.com/exif/1.0/aux/" '
           b'xmlns:xmpMM="http://ns.adobe.com/xap/1.0/mm/" '
           b'xmlns:dc="http://purl.org/dc/elements/1.1/">'
           b'<rdf:RDF><rdf:Description exif:GPSLatitude="48,1" '
           b'aux:SerialNumber="XMP-SERIAL" xmpMM:DocumentID="DOC-123" '
           b'dc:format="image/jpeg">'
           b'<dc:creator><rdf:Seq><rdf:li>Synthetic Owner</rdf:li></rdf:Seq></dc:creator>'
           b'</rdf:Description></rdf:RDF></x:xmpmeta>')
    return XMP + xml


def iptc_fixture():
    def dataset(number, value):
        return b"\x1c\x02" + bytes((number,)) + struct.pack(">H", len(value)) + value
    body = dataset(90, b"Synthetic City") + dataset(120, b"Retained caption")
    return (b"Photoshop 3.0\0" + b"8BIM\x04\x04\0\0" +
            struct.pack(">I", len(body)) + body)


def image(*extras):
    return BASE[:2] + b"".join(extras) + BASE[2:]


def orientation(data):
    payload = next((p for k, p in segments(data) if k == 0xE1 and p.startswith(EXIF)), None)
    if payload is None:
        return None
    tiff = payload[6:]
    root = struct.unpack_from("<I", tiff, 4)[0]
    count = struct.unpack_from("<H", tiff, root)[0]
    for i in range(count):
        at = root + 2 + i * 12
        if struct.unpack_from("<H", tiff, at)[0] == 0x0112:
            return struct.unpack_from("<H", tiff, at + 8)[0]
    return None


class ScrubPrivacyTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="photoc-scrub-privacy-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "input.jpg"
        self.output = self.root / "input.scrubbed.jpg"

    def run_cli(self, *args, code=0):
        result = subprocess.run([BINARY, *map(str, args)], capture_output=True,
                                text=True, timeout=15)
        self.assertEqual(result.returncode, code, result.stdout + result.stderr)
        return result

    def fixture(self):
        icc = next(p for k, p in segments((FIX / "icc_only.jpg").read_bytes())
                   if k == 0xE2 and p.startswith(ICC))
        return image(marker(0xE1, exif_fixture()), marker(0xE1, xmp_fixture()),
                     marker(0xE2, icc), marker(0xED, iptc_fixture()),
                     marker(0xFE, b"comment to remove"))

    def test_privacy_selective_copy_and_decode(self):
        before = self.fixture()
        self.source.write_bytes(before)
        result = self.run_cli("scrub", self.source, "--privacy", "-v")
        after = self.output.read_bytes()
        self.assertEqual(self.source.read_bytes(), before)
        self.assertEqual(orientation(after), 6)
        self.assertIn(b"Retained caption", after)
        self.assertIn(b"image/jpeg", after)
        self.assertIn(b"opaque private maker bytes", after)
        for forbidden in (b"Synthetic City", b"Synthetic Artist", b"Synthetic Owner",
                          b"BODY-12345", b"LENS-12345", b"UNIQUE-12345",
                          b"XMP-SERIAL", b"DOC-123", b"GPSLatitude"):
            self.assertNotIn(forbidden, after)
        self.assertIn("may retain private information", result.stderr)
        self.assertNotIn("48,1", result.stdout + result.stderr)
        self.assertNotIn("BODY-12345", result.stdout + result.stderr)
        self.assertEqual([(k, p) for k, p in segments(after) if k == 0xE2],
                         [(k, p) for k, p in segments(before) if k == 0xE2])
        self.run_cli("focus", self.output)

    def test_all_metadata_keeps_icc_and_orientation(self):
        before = self.fixture()
        self.source.write_bytes(before)
        self.run_cli("scrub", self.source, "--all-metadata")
        after = self.output.read_bytes()
        self.assertEqual(self.source.read_bytes(), before)
        self.assertEqual(orientation(after), 6)
        self.assertEqual([(k, p) for k, p in segments(after) if k == 0xE2],
                         [(k, p) for k, p in segments(before) if k == 0xE2])
        self.assertNotIn(b"opaque private maker bytes", after)
        self.assertNotIn(b"comment to remove", after)
        self.assertFalse(any(k in (0xED, 0xFE) for k, _ in segments(after)))
        self.assertFalse(any(p.startswith(XMP) for _, p in segments(after)))
        self.run_cli("focus", self.output)

    def test_in_place_collision_and_malformed(self):
        before = self.fixture()
        self.source.write_bytes(before)
        self.output.write_bytes(b"sentinel")
        self.run_cli("scrub", self.source, "--privacy", code=1)
        self.assertEqual(self.output.read_bytes(), b"sentinel")
        self.assertEqual(self.source.read_bytes(), before)
        self.run_cli("scrub", self.source, "--privacy", "--in-place")
        self.assertNotEqual(self.source.read_bytes(), before)
        self.assertFalse(any(p.startswith(XMP) and b"GPSLatitude" in p
                             for _, p in segments(self.source.read_bytes())))
        self.source.write_bytes(image(marker(0xE1, XMP + b"<broken")))
        invalid = self.source.read_bytes()
        self.run_cli("scrub", self.source, "--privacy", "--in-place", code=1)
        self.assertEqual(self.source.read_bytes(), invalid)

    def test_extended_xmp_refuses_privacy_and_modes_exclusive(self):
        self.source.write_bytes((FIX / "xmp_extended.jpg").read_bytes())
        before = self.source.read_bytes()
        self.run_cli("scrub", self.source, "--privacy", code=1)
        self.assertEqual(self.source.read_bytes(), before)
        self.assertFalse(self.output.exists())
        self.run_cli("scrub", self.source, "--gps", "--privacy", code=2)
        self.run_cli("scrub", self.source, "--privacy", "--all-metadata", code=2)

    def test_all_metadata_transfers_xmp_only_orientation(self):
        packet = (b'<x:xmpmeta xmlns:x="adobe:ns:meta/" '
                  b'xmlns:tiff="http://ns.adobe.com/tiff/1.0/">'
                  b'<tiff:Orientation>8</tiff:Orientation></x:xmpmeta>')
        self.source.write_bytes(image(marker(0xE1, XMP + packet)))
        self.run_cli("scrub", self.source, "--all-metadata")
        after = self.output.read_bytes()
        self.assertEqual(orientation(after), 8)
        self.assertFalse(any(p.startswith(XMP) for _, p in segments(after)))
        self.run_cli("focus", self.output)

    def test_privacy_without_exif_and_multichunk_icc_all_mode(self):
        self.source.write_bytes(image(marker(0xE1, xmp_fixture())))
        self.run_cli("scrub", self.source, "--privacy")
        self.assertNotIn(b"GPSLatitude", self.output.read_bytes())
        self.output.unlink()
        before = (FIX / "icc_multi.jpg").read_bytes()
        self.source.write_bytes(before)
        self.run_cli("scrub", self.source, "--all-metadata")
        after = self.output.read_bytes()
        self.assertEqual(orientation(after), 6)
        self.assertEqual([(k, p) for k, p in segments(after) if k == 0xE2],
                         [(k, p) for k, p in segments(before) if k == 0xE2])
        self.run_cli("focus", self.output)

    def test_in_place_rejects_linked_source_and_bad_iptc(self):
        before = self.fixture()
        self.source.write_bytes(before)
        sibling = self.root / "linked.jpg"
        os.link(self.source, sibling)
        self.run_cli("scrub", self.source, "--privacy", "--in-place", code=1)
        self.assertEqual(self.source.read_bytes(), before)
        self.assertEqual(sibling.read_bytes(), before)
        sibling.unlink()
        broken = image(marker(0xED, b"Photoshop 3.0\0" +
                              b"8BIM\x04\x04\0\0\xff\xff\xff\xff"))
        self.source.write_bytes(broken)
        self.run_cli("scrub", self.source, "--privacy", "--in-place", code=1)
        self.assertEqual(self.source.read_bytes(), broken)

    def test_mpf_index_is_not_rewritten_with_stale_offsets(self):
        before = image(marker(0xE1, exif_fixture()),
                       marker(0xE2, b"MPF\0" + b"opaque index"))
        self.source.write_bytes(before)
        self.run_cli("scrub", self.source, "--all-metadata", code=1)
        self.assertEqual(self.source.read_bytes(), before)
        self.assertFalse(self.output.exists())

    def test_invalid_orientation_is_not_dropped(self):
        payload = bytearray(exif_fixture())
        tiff_start = 6
        root = struct.unpack_from("<I", payload, tiff_start + 4)[0]
        count = struct.unpack_from("<H", payload, tiff_start + root)[0]
        for i in range(count):
            at = tiff_start + root + 2 + i * 12
            if struct.unpack_from("<H", payload, at)[0] == 0x0112:
                struct.pack_into("<H", payload, at + 8, 9)
                break
        else:
            self.fail("orientation fixture missing")
        before = image(marker(0xE1, payload))
        self.source.write_bytes(before)
        self.run_cli("scrub", self.source, "--all-metadata", "--in-place", code=1)
        self.assertEqual(self.source.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()

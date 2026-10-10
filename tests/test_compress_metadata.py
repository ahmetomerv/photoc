#!/usr/bin/env python3
"""Opaque JPEG marker preservation through the real CLI; no external tools."""

import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests/fixtures/jpeg_metadata"
BINARY = os.environ["PHOTOC_COMPRESS_METADATA_BINARY"]
EXIF = b"Exif\0\0"
ICC = b"ICC_PROFILE\0"
XMP = b"http://ns.adobe.com/xap/1.0/\0"
EXTENDED = b"http://ns.adobe.com/xmp/extension/\0"
BASE = (FIXTURES / "no_metadata.jpg").read_bytes()
# Reference output published by the pre-change compress pipeline. The buffer
# single-read path must reproduce these exact bytes.
GOLDEN_QUALITY_61 = FIXTURES / "all_metadata.quality61.compressed.jpg"


def marker(kind, payload):
    return bytes((0xFF, kind)) + struct.pack(">H", len(payload) + 2) + payload


def segments(data):
    """Independent marker reader, including entropy escapes and restart markers."""
    if data[:2] != b"\xff\xd8":
        raise ValueError("missing SOI")
    position = 2
    scanning = False
    while position < len(data):
        if scanning:
            while position < len(data):
                if data[position] != 0xFF:
                    position += 1
                    continue
                position += 1
                while position < len(data) and data[position] == 0xFF:
                    position += 1
                if position == len(data):
                    raise ValueError("truncated entropy marker")
                code = data[position]
                position += 1
                if code == 0 or 0xD0 <= code <= 0xD7:
                    continue
                break
            else:
                raise ValueError("missing EOI")
        else:
            if data[position] != 0xFF:
                raise ValueError("missing marker prefix")
            position += 1
            while position < len(data) and data[position] == 0xFF:
                position += 1
            if position == len(data):
                raise ValueError("truncated marker")
            code = data[position]
            position += 1
        if code == 0xD9:
            return
        if code == 1:
            scanning = False
            continue
        if position + 2 > len(data):
            raise ValueError("missing length")
        length = int.from_bytes(data[position:position + 2], "big")
        if length < 2 or position + length > len(data):
            raise ValueError("invalid segment length")
        payload = data[position + 2:position + length]
        yield code, payload
        position += length
        scanning = code == 0xDA
    raise ValueError("missing EOI")


def metadata(data):
    return [(kind, payload) for kind, payload in segments(data)
            if (kind == 0xE1 and payload.startswith((EXIF, XMP, EXTENDED))) or
            (kind == 0xE2 and payload.startswith(ICC))]


def profile_bytes(data):
    chunks = [payload for kind, payload in metadata(data) if kind == 0xE2]
    return b"".join(payload[14:] for payload in sorted(chunks, key=lambda p: p[12]))


def orientation(data):
    payload = next((p for k, p in metadata(data) if p.startswith(EXIF)), None)
    if payload is None:
        return None
    tiff = payload[6:]
    order = "<" if tiff[:2] == b"II" else ">"
    offset = struct.unpack_from(order + "I", tiff, 4)[0]
    count = struct.unpack_from(order + "H", tiff, offset)[0]
    for i in range(count):
        entry = offset + 2 + 12 * i
        if struct.unpack_from(order + "H", tiff, entry)[0] == 0x0112:
            return struct.unpack_from(order + "H", tiff, entry + 8)[0]
    return None


class CompressMetadataTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="photoc-marker-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()

    def invoke(self, *args, expected=0):
        result = subprocess.run([BINARY, *map(str, args)], cwd=self.root,
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def copy(self, name, relative=None):
        path = self.root / (relative or name)
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(FIXTURES / name, path)
        return path

    def check_preserved(self, source, output):
        before = source.read_bytes()
        after = output.read_bytes()
        self.assertEqual(metadata(after), metadata(before))
        self.assertEqual(profile_bytes(after), profile_bytes(before))
        if profile_bytes(before):
            def component_count(data):
                return next(p[5] for k, p in segments(data) if k in (0xC0, 0xC1, 0xC2))
            self.assertEqual(component_count(after), component_count(before))
        # EXIF's standard values are still readable; dimensions do not rotate.
        old = json.loads(self.invoke("exif", source, "--json").stdout)
        new = json.loads(self.invoke("exif", output, "--json").stdout)
        self.assertEqual(new["image"], {"width": 3, "height": 2,
                                      "orientation": orientation(output.read_bytes())})
        for field in ("image", "camera", "exposure", "date", "location"):
            self.assertEqual(old[field], new[field])
        # Focus performs pixel decoding, rather than merely reading the JPEG header.
        self.invoke("focus", output, "--quiet")
        if any(p.startswith(EXIF) for _, p in metadata(before)):
            self.assertEqual(orientation(after), orientation(before))
            # Invented MakerNotes are carried intact rather than reserialized.
            self.assertIn(b"photoc synthetic MakerNote\0\xff\x00", after)

    def test_fixture_generation_is_deterministic(self):
        spec = importlib.util.spec_from_file_location("marker_fixtures", ROOT / "scripts/make-jpeg-segment-fixtures.py")
        generator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(generator)
        for name, expected in generator.fixtures().items():
            self.assertEqual((FIXTURES / name).read_bytes(), expected)

    def test_all_metadata_combinations_in_quality_and_target_modes(self):
        for fixture in ("exif_only.jpg", "icc_only.jpg", "xmp_only.jpg", "all_metadata.jpg",
                        "icc_multi.jpg", "no_metadata.jpg", "xmp_extended.jpg", "gray_icc.jpg"):
            for mode, options in (("quality", ("--quality", "61")), ("target", ("--target", "1MB"))):
                with self.subTest(fixture=fixture, mode=mode):
                    source = self.copy(fixture, f"{mode}/{fixture}")
                    original = source.read_bytes()
                    output = source.with_name(source.stem + ".compressed.jpg")
                    self.invoke("compress", source, *options)
                    self.check_preserved(source, output)
                    self.assertEqual(source.read_bytes(), original)
                    result = self.invoke("compress", source, *options, expected=1)
                    self.assertIn("collision", result.stderr)
                    self.assertEqual(source.read_bytes(), original)
                    self.assertEqual(metadata(output.read_bytes()), metadata(original))
        multi = metadata((FIXTURES / "icc_multi.jpg").read_bytes())
        self.assertEqual([p[12] for k, p in multi if k == 0xE2], [3, 1, 2])

    def test_all_exif_orientation_values(self):
        payload = next(p for k, p in metadata((FIXTURES / "exif_only.jpg").read_bytes()) if p.startswith(EXIF))
        tiff = payload[6:]
        offset = struct.unpack_from("<I", tiff, 4)[0]
        count = struct.unpack_from("<H", tiff, offset)[0]
        position = next(offset + 2 + i * 12 + 8 for i in range(count)
                        if struct.unpack_from("<H", tiff, offset + 2 + i * 12)[0] == 0x0112)
        for value in range(1, 9):
            edited = bytearray(payload)
            struct.pack_into("<H", edited, 6 + position, value)
            source = self.root / f"orientation-{value}.jpg"
            source.write_bytes(BASE[:2] + marker(0xE1, edited) + BASE[2:])
            self.invoke("compress", source)
            self.check_preserved(source, source.with_name(source.stem + ".compressed.jpg"))

    def test_target_search_accounts_for_every_marker_byte(self):
        source = self.copy("icc_multi.jpg")
        self.invoke("compress", source, "--quality", "60")
        baseline = source.with_name(source.stem + ".compressed.jpg")
        target = baseline.stat().st_size
        baseline.unlink()
        result = self.invoke("compress", source, "--target", target)
        output = baseline
        self.assertLessEqual(output.stat().st_size, target)
        quality = int(next(line.split(": ")[1] for line in result.stdout.splitlines() if line.startswith("Quality:")))
        self.assertGreaterEqual(quality, 60)
        self.check_preserved(source, output)
        if quality < 100:
            next_source = self.copy("icc_multi.jpg", "next.jpg")
            self.invoke("compress", next_source, "--quality", quality + 1)
            self.assertGreater(next_source.with_name("next.compressed.jpg").stat().st_size, target)
        output.unlink()
        result = self.invoke("compress", source, "--target", "1B", "--min-quality", "40", expected=1)
        self.assertIn("target cannot be reached", result.stderr)
        self.check_preserved(source, output)

    def _compress_bytes(self, fixture, relative, quality):
        source = self.copy(fixture, relative)
        self.invoke("compress", source, "--quality", quality)
        return source.with_name(source.stem + ".compressed.jpg").read_bytes()

    def test_output_bytes_are_unchanged(self):
        """The single-read path must publish the pre-change bytes exactly.

        Independent runs over the same content at different paths pin the
        published file, and every result is compared byte for byte against the
        checked-in golden produced by the pre-change pipeline.
        """
        golden = GOLDEN_QUALITY_61.read_bytes()
        first = self._compress_bytes("all_metadata.jpg", "one/photo.jpg", "61")
        second = self._compress_bytes("all_metadata.jpg", "two/other.jpg", "61")
        reference = self._compress_bytes("all_metadata.jpg", "three/ref.jpg", "61")
        self.assertEqual(first, second)
        self.assertEqual(second, reference)
        self.assertEqual(first, golden)
        self.assertEqual(second, golden)
        self.assertEqual(reference, golden)
        self.assertGreater(len(first), 4)
        self.assertEqual(first[:2], b"\xff\xd8")
        self.assertEqual(first[-2:], b"\xff\xd9")

    def test_target_reuses_the_chosen_encode(self):
        source = self.copy("all_metadata.jpg")
        self.invoke("compress", source, "--quality", "60")
        baseline = source.with_name(source.stem + ".compressed.jpg")
        target = baseline.stat().st_size
        baseline.unlink()
        result = self.invoke("compress", source, "--target", target)
        output = source.with_name(source.stem + ".compressed.jpg")
        reused = output.read_bytes()
        output.unlink()
        quality = int(next(line.split(": ")[1] for line in result.stdout.splitlines()
                           if line.startswith("Quality:")))
        reference = self.copy("all_metadata.jpg", "reference.jpg")
        self.invoke("compress", reference, "--quality", str(quality))
        expected = reference.with_name("reference.compressed.jpg").read_bytes()
        self.assertEqual(reused, expected)

    def test_malformed_icc_sequences_fail_without_publishing(self):
        cases = [ICC[:-1], ICC[:-1] + b"X\x01\x01data", ICC, ICC + b"\x01", ICC + b"\0\x01data", ICC + b"\x01\0data",
                 ICC + b"\x02\x01data", ICC + b"\x01\x02data", ICC + b"\x01\x01"]
        for i, payload in enumerate(cases):
            for mode, options in (("quality", ()), ("target", ("--target", "2MB"))):
                with self.subTest(i=i, mode=mode):
                    source = self.root / f"bad-{mode}-{i}.jpg"
                    original = BASE[:2] + marker(0xE2, payload) + BASE[2:]
                    source.write_bytes(original)
                    result = self.invoke("compress", source, *options, "--quiet", expected=1)
                    self.assertIn("invalid ICC", result.stderr)
                    self.assertEqual(result.stdout, "")
                    self.assertEqual(source.read_bytes(), original)
                    self.assertFalse(source.with_name(source.stem + ".compressed.jpg").exists())
        for i, chunks in enumerate(((ICC + b"\x01\x02a", ICC + b"\x01\x02b"),
                                    (ICC + b"\x01\x02a", ICC + b"\x02\x03b"))):
            source = self.root / f"inconsistent-{i}.jpg"
            source.write_bytes(BASE[:2] + b"".join(marker(0xE2, p) for p in chunks) + BASE[2:])
            self.assertIn("invalid ICC", self.invoke("compress", source, expected=1).stderr)
        self.assertFalse(list(self.root.glob("*.photoc-*")))

    def test_metadata_after_scan_is_refused(self):
        for i, payload in enumerate((XMP + b"opaque", EXTENDED + b"opaque", ICC + b"\x01\x01profile")):
            source = self.root / f"late-{i}.jpg"
            original = BASE[:-2] + marker(0xE1 if i < 2 else 0xE2, payload) + BASE[-2:]
            source.write_bytes(original)
            result = self.invoke("compress", source, expected=1)
            self.assertIn("metadata layout", result.stderr)
            self.assertEqual(source.read_bytes(), original)
            self.assertFalse(source.with_name(source.stem + ".compressed.jpg").exists())

    def test_only_whitelisted_metadata_is_copied(self):
        supported = b"".join(marker(k, p) for k, p in metadata((FIXTURES / "all_metadata.jpg").read_bytes()))
        unknown = marker(0xE1, b"vendor APP1") + marker(0xE2, b"MPF\0old-stream-offsets") + marker(0xED, b"Photoshop 3.0\0not-supported")
        source = self.root / "unknown.jpg"
        source.write_bytes(BASE[:2] + supported + unknown + BASE[2:])
        self.invoke("compress", source)
        output = source.with_name("unknown.compressed.jpg")
        self.check_preserved(source, output)
        self.assertFalse(any(p.startswith((b"vendor", b"MPF", b"Photoshop")) for k, p in segments(output.read_bytes())))

    def test_maximum_app2_payload_and_empty_intermediate_chunk(self):
        source = self.root / "large-chunk.jpg"
        profile = bytes(range(256)) * 256
        source.write_bytes(BASE[:2] + marker(0xE2, ICC + b"\x02\x03") +
                           marker(0xE2, ICC + b"\x01\x03" + profile[:65519]) +
                           marker(0xE2, ICC + b"\x03\x03" + profile[65519:]) + BASE[2:])
        self.invoke("compress", source, "--target", "1MB")
        self.check_preserved(source, source.with_name("large-chunk.compressed.jpg"))

    def test_extended_xmp_is_preserved_without_xml_interpretation(self):
        source = self.root / "opaque.jpg"
        source.write_bytes(BASE[:2] + marker(0xE1, XMP + b"not XML\0\xff\x00") +
                           marker(0xE1, EXTENDED + b"not a valid GUID/offset header") + BASE[2:])
        self.invoke("compress", source)
        self.check_preserved(source, source.with_name("opaque.compressed.jpg"))

    def test_recursive_directory_and_partial_failure(self):
        for options in (("--quality", "45"), ("--target", "1MB")):
            folder = self.root / ("quality" if options[0] == "--quality" else "target")
            inputs = [self.copy("all_metadata.jpg", str(folder.relative_to(self.root) / "a.jpg")),
                      self.copy("icc_multi.jpg", str(folder.relative_to(self.root) / "nested/b.jpeg")),
                      self.copy("icc_bad_sequence.jpg", str(folder.relative_to(self.root) / "bad.jpg"))]
            before = [p.read_bytes() for p in inputs]
            destination = folder.parent / (folder.name + "-output")
            result = self.invoke("compress", folder, "--recursive", "--output-dir", destination, *options, expected=1)
            self.assertIn("Files processed: 2", result.stdout)
            self.assertIn("Files failed: 1", result.stdout)
            self.assertIn("invalid ICC", result.stderr)
            self.check_preserved(inputs[0], destination / "a.compressed.jpg")
            self.check_preserved(inputs[1], destination / "nested/b.compressed.jpeg")
            self.assertFalse((destination / "bad.compressed.jpg").exists())
            self.assertEqual([p.read_bytes() for p in inputs], before)

    def test_scrub_keeps_its_existing_semantics_even_with_invalid_icc(self):
        for malformed in (False, True):
            source = self.copy("all_metadata.jpg", f"scrub-{malformed}.jpg")
            if malformed:
                data = source.read_bytes()
                position = data.index(ICC) + len(ICC)
                source.write_bytes(data[:position] + b"\0" + data[position + 1:])
            original = source.read_bytes()
            before = [(k, p) for k, p in metadata(original) if not p.startswith(EXIF)]
            self.invoke("scrub", source, "--gps")
            output = source.with_name(source.stem + ".scrubbed.jpg")
            after = [(k, p) for k, p in metadata(output.read_bytes()) if not p.startswith(EXIF)]
            self.assertEqual(before, after)
            self.assertFalse(json.loads(self.invoke("exif", output, "--json").stdout)["location"]["has_gps"])
            self.assertEqual(orientation(output.read_bytes()), 6)
            self.assertEqual(source.read_bytes(), original)
            self.invoke("scrub", source, "--gps", "--in-place")
            self.assertEqual([(k, p) for k, p in metadata(source.read_bytes()) if not p.startswith(EXIF)], before)
            self.assertFalse(json.loads(self.invoke("exif", source, "--json").stdout)["location"]["has_gps"])


if __name__ == "__main__":
    unittest.main()

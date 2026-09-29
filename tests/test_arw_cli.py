#!/usr/bin/env python3
"""Sony TIFF metadata and command boundaries; no RAW development."""
import json
import os
from pathlib import Path
import random
import shutil
import struct
import sys
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from make_arw_fixtures import make_arw
BINARY = Path(os.environ.get("PHOTOC_ARW_TEST_BINARY", ROOT / "build/photoc")).resolve()
JPEG = ROOT / "tests/fixtures/jpeg"


def entries(data, offset=8, endian="<"):
    count = struct.unpack_from(endian + "H", data, offset)[0]
    return {struct.unpack_from(endian + "H", data, offset + 2 + index * 12)[0]: offset + 2 + index * 12
            for index in range(count)}


def pointed(data, tag, offset=8, endian="<"):
    return struct.unpack_from(endian + "I", data, entries(data, offset, endian)[tag] + 8)[0]


class ARWCLI(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="photoc-arw-")
        self.directory = Path(self.temp.name).resolve()

    def tearDown(self):
        self.temp.cleanup()

    def raw(self, name="DSC00001.ARW", data=None, **kwargs):
        path = self.directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(make_arw(**kwargs) if data is None else data)
        return path

    def run_cli(self, *args, code=0):
        result = subprocess.run([str(BINARY), *map(str, args)], capture_output=True, timeout=20)
        self.assertEqual(result.returncode, code, result.stderr.decode(errors="replace"))
        return result

    def exif(self, path, **kwargs):
        return json.loads(self.run_cli("exif", path, "--json", **kwargs).stdout)

    def test_sony_and_endian_metadata(self):
        for endian in ("<", ">"):
            path = self.raw(endian=endian)
            before = (path.read_bytes(), path.stat().st_mtime_ns, path.stat().st_mode)
            data = self.exif(path)
            self.assertEqual(data["file"]["format"], "sony_arw")
            self.assertEqual(data["image"], {"width": 5472, "height": 3648, "orientation": 6})
            self.assertEqual(data["camera"], {"make": "SONY", "model": "DSC-RX100M7A"})
            self.assertEqual(data["date"]["captured"], "2026:09:29 12:34:56")
            self.assertEqual(data["exposure"], {"iso": 1600, "aperture": 4, "exposure_time_seconds": 0.005, "focal_length_mm": 50})
            self.assertTrue(data["location"]["has_gps"])
            self.assertAlmostEqual(data["location"]["latitude"], -37.8083333333)
            self.assertAlmostEqual(data["location"]["longitude"], -122.4041666667)
            self.assertEqual((path.read_bytes(), path.stat().st_mtime_ns, path.stat().st_mode), before)
            self.assertEqual(self.run_cli("exif", path, "--quiet", "--json").stdout, self.run_cli("exif", path, "--verbose", "--json").stdout)
        text = self.run_cli("exif", path).stdout
        self.assertIn(b"5472 x 3648 pixels", text)
        self.assertIn(b"Orientation: 6", text)

    def test_missing_and_dimension_sources(self):
        path = self.raw(missing=True)
        data = self.exif(path)
        self.assertEqual(data["image"], {"width": None, "height": None, "orientation": None})
        self.assertIsNone(data["camera"]["model"])
        self.assertIsNone(data["date"]["captured"])
        self.assertTrue(all(value is None for value in data["exposure"].values()))
        self.assertFalse(data["location"]["has_gps"])
        self.assertIn(b"Dimensions: Unavailable", self.run_cli("exif", path).stdout)
        path = self.raw(exif_dimensions=False)
        self.assertEqual(self.exif(path)["image"]["width"], 5504)
        path = self.raw(exif_dimensions=False, raw_dimensions=False)
        self.assertIsNone(self.exif(path)["image"]["width"])  # Never use preview dimensions.
        path = self.raw(orientation=0)
        self.assertIsNone(self.exif(path)["image"]["orientation"])
        path = self.raw(missing=True, maker=None)
        self.assertIsNone(self.exif(path)["camera"]["make"])

    def test_malformed_offsets_and_truncation(self):
        base = make_arw()
        root = entries(base)
        malformed = [b"", b"garbage", base[:7], base[:-1]]
        for offset, value in ((4, 0xffffffff), (root[0x8769] + 8, 0xffffffff), (root[0x014a] + 8, 8),
                              (root[0x0110] + 8, 0xffffffff)):
            data = bytearray(base)
            struct.pack_into("<I", data, offset, value)
            malformed.append(bytes(data))
        data = bytearray(base)
        struct.pack_into("<I", data, root[0x0110] + 4, 0xffffffff)
        malformed.append(bytes(data))
        data = bytearray(base)
        next_offset = 8 + 2 + len(root) * 12
        struct.pack_into("<I", data, next_offset, 8)
        malformed.append(bytes(data))
        data = bytearray(base)
        struct.pack_into("<H", data, 8, 65535)
        malformed.append(bytes(data))
        data = bytearray(base)
        struct.pack_into("<H", data, root[0x0110], 0x010f)  # Duplicate tag.
        malformed.append(bytes(data))
        for index, data in enumerate(malformed):
            path = self.raw(data=data)
            result = self.run_cli("exif", path, "--quiet", "--json", code=1)
            self.assertEqual(result.stdout, b"", index)
            self.assertTrue(result.stderr)
            self.assertEqual(path.read_bytes(), data)

    def test_invalid_field_values_stay_missing(self):
        data = bytearray(make_arw())
        exif = pointed(data, 0x8769)
        exposure = pointed(data, 0x829a, exif)
        struct.pack_into("<I", data, exposure + 4, 0)  # Zero denominator.
        gps = pointed(data, 0x8825)
        ref = entries(data, gps)[1]
        data[ref + 8] = ord("?")
        result = self.exif(self.raw(data=bytes(data)))
        self.assertIsNone(result["exposure"]["exposure_time_seconds"])
        self.assertFalse(result["location"]["has_gps"])

    def test_resource_limits_and_large_dimensions(self):
        data = bytearray(make_arw())
        model = entries(data)[0x0110]
        struct.pack_into("<I", data, model + 4, 65537)
        struct.pack_into("<I", data, model + 8, len(data))
        data += b"A" * 65536 + b"\0"
        result = self.run_cli("exif", self.raw(data=bytes(data)), "--json", code=1)
        self.assertIn(b"resource limits", result.stderr)
        self.assertEqual(result.stdout, b"")

        data = bytearray(make_arw())
        exif = pointed(data, 0x8769)
        for tag in (0xa002, 0xa003):
            struct.pack_into("<I", data, entries(data, exif)[tag] + 8, 0xffffffff)
        image = self.exif(self.raw(data=bytes(data)))["image"]
        self.assertEqual(image["width"], 0xffffffff)
        self.assertEqual(image["height"], 0xffffffff)  # Tags never allocate pixels.

    def test_unsupported_raw_formats(self):
        for suffix in ("NEF", "CR2", "CR3", "DNG", "SR2", "SRF"):
            result = self.run_cli("exif", self.raw(name="other." + suffix), "--json", code=1)
            self.assertIn(b"unsupported", result.stderr)
        for kwargs in ({"maker": b"NIKON\0"}, {"dng": True}):
            result = self.run_cli("exif", self.raw(**kwargs), code=1)
            self.assertIn(b"unsupported", result.stderr)
        data = bytearray(make_arw())
        struct.pack_into("<H", data, 2, 43)
        self.assertIn(b"unsupported", self.run_cli("exif", self.raw(data=bytes(data)), code=1).stderr)

    def test_mixed_collection_stats_and_recursion(self):
        self.raw(name="a.ArW")
        self.raw(name="nested/b.arw", missing=True)
        broken = self.raw(name="broken.ARW", data=b"bad")
        shutil.copyfile(JPEG / "with_exif.jpg", self.directory / "photo.JPG")
        self.raw(name="unsupported.nef")
        (self.directory / "link.arw").symlink_to(broken)
        flat = json.loads(self.run_cli("stats", self.directory, "--json").stdout)
        self.assertEqual(flat["scan"]["jpeg_files_found"], 1)
        self.assertEqual(flat["scan"]["arw_files_found"], 2)
        self.assertEqual(flat["scan"]["metadata_files_found"], 3)
        self.assertEqual(flat["scan"]["photos_parsed"], 2)
        self.assertEqual(flat["scan"]["errors"], 1)
        self.assertEqual(flat["scan"]["skipped_files"], 2)
        recursive = json.loads(self.run_cli("stats", self.directory, "--recursive", "--json").stdout)
        self.assertEqual(recursive["scan"]["arw_files_found"], 3)
        self.assertEqual(recursive["scan"]["photos_parsed"], 3)
        self.assertEqual(recursive["storage"]["total_bytes"], sum(p.stat().st_size for p in (self.directory / "a.ArW", self.directory / "nested/b.arw", self.directory / "photo.JPG")))

    def test_jpeg_only_commands_exclude_arw(self):
        raw = self.raw()
        original = raw.read_bytes()
        for args in (("compress",), ("focus",), ("check",), ("query",), ("scrub", "--gps")):
            self.run_cli(args[0], raw, *args[1:], code=1)
            self.assertEqual(raw.read_bytes(), original)
        for args in (("compress",), ("focus",), ("check",), ("query",), ("scrub", "--gps")):
            self.run_cli(args[0], self.directory, *args[1:])
            self.assertEqual(raw.read_bytes(), original)
        self.assertEqual(sorted(p.name for p in self.directory.iterdir()), [raw.name])
        jpg = self.directory / "photo.jpg"
        shutil.copyfile(JPEG / "with_gps.jpeg", jpg)
        self.assertEqual(self.run_cli("query", self.directory).stdout, (str(jpg) + "\n").encode())
        focus = json.loads(self.run_cli("focus", self.directory, "--json").stdout)
        self.assertEqual(focus["summary"]["photos_analyzed"], 1)
        check = json.loads(self.run_cli("check", self.directory, "--json").stdout)
        self.assertEqual(check["summary"]["files_checked"], 1)
        self.run_cli("compress", self.directory)
        self.run_cli("scrub", self.directory, "--gps")
        self.assertTrue((self.directory / "photo.compressed.jpg").is_file())
        self.assertTrue((self.directory / "photo.scrubbed.jpg").is_file())
        self.assertFalse((self.directory / "DSC00001.compressed.ARW").exists())
        self.assertFalse((self.directory / "DSC00001.scrubbed.ARW").exists())
        self.assertEqual(raw.read_bytes(), original)

    def test_batched_mixed_metadata_scan(self):
        for index in range(40):
            self.raw(name=f"{index:03}.ARW", endian="<" if index % 2 else ">")
        for index in range(8):
            shutil.copyfile(JPEG / "with_exif.jpg", self.directory / f"photo{index}.jpg")
        first = self.run_cli("stats", self.directory, "--json").stdout
        second = self.run_cli("stats", self.directory, "--json").stdout
        self.assertEqual(first, second)
        scan = json.loads(first)["scan"]
        self.assertEqual(scan["arw_files_found"], 40)
        self.assertEqual(scan["jpeg_files_found"], 8)
        self.assertEqual(scan["photos_parsed"], 48)
        self.assertEqual(scan["errors"], 0)

    def test_rename_and_sort_preserve_bytes_and_extensions(self):
        raw = self.raw()
        original = raw.read_bytes()
        jpeg = self.directory / "photo.JPG"
        shutil.copyfile(JPEG / "with_exif.jpg", jpeg)
        jpeg_original = jpeg.read_bytes()
        self.run_cli("rename", self.directory, "--format", "{date}_{sequence}.{ext}")
        self.assertTrue(raw.exists())
        self.run_cli("rename", self.directory, "--format", "{date}_{sequence}.{ext}", "--apply")
        renamed = self.directory / "2026-09-29_0001.ARW"
        self.assertEqual(renamed.read_bytes(), original)
        self.run_cli("sort", self.directory, "--by", "date")
        self.assertTrue(renamed.exists())
        self.run_cli("sort", self.directory, "--by", "date", "--apply")
        sorted_path = self.directory / "2026/09/29" / renamed.name
        self.assertEqual(sorted_path.read_bytes(), original)
        jpeg_sorted = self.directory / "2026/09/27/2026-09-27_0002.JPG"
        self.assertEqual(jpeg_sorted.read_bytes(), jpeg_original)

    def test_preflight_blocks_collisions_and_bad_metadata(self):
        raw = self.raw()
        other = self.raw(name="other.ARW")
        snapshot = {p: p.read_bytes() for p in (raw, other)}
        self.run_cli("rename", self.directory, "--format", "same.{ext}", "--apply", code=1)
        self.assertTrue(all(p.read_bytes() == data for p, data in snapshot.items()))
        missing = self.raw(name="missing.arw", missing=True)
        self.run_cli("sort", self.directory, "--by", "date", "--apply", code=1)
        self.assertTrue(all(p.exists() for p in (raw, other, missing)))
        missing.unlink()
        collision = self.directory / "2026/09/29" / raw.name
        collision.parent.mkdir(parents=True)
        collision.write_bytes(b"Existing file")
        self.run_cli("sort", self.directory, "--by", "date", "--apply", code=1)
        self.assertEqual(collision.read_bytes(), b"Existing file")
        self.assertTrue(raw.exists() and other.exists())

    def test_arw_sort_runtime_rollback(self):
        if os.geteuid() == 0:
            self.skipTest("permission failures require an unprivileged user")
        early = self.raw(name="a.ARW")
        late = self.raw(name="z.ARW")
        data = bytearray(early.read_bytes())
        exif = pointed(data, 0x8769)
        date = pointed(data, 0x9003, exif)
        data[date:date + 19] = b"2024:01:02 03:04:05"
        early.write_bytes(data)
        original = {early: early.read_bytes(), late: late.read_bytes()}
        folder = self.directory / "2026/09/29"
        folder.mkdir(parents=True)
        folder.chmod(0o500)
        try:
            result = self.run_cli("sort", self.directory, "--by", "date", "--apply", code=1)
            self.assertIn(b"1 rolled back", result.stdout)
            self.assertTrue(all(path.read_bytes() == content for path, content in original.items()))
        finally:
            folder.chmod(0o700)

    def test_bounded_mutated_inputs(self):
        randomizer = random.Random(20260929)
        original = make_arw()
        for index in range(80):
            data = bytearray(original)
            for _ in range(1 + index % 4):
                location = randomizer.randrange(len(data))
                data[location] ^= randomizer.randrange(1, 256)
            path = self.raw(data=bytes(data))
            result = subprocess.run([str(BINARY), "exif", str(path), "--json"], capture_output=True, timeout=10)
            self.assertIn(result.returncode, (0, 1), result.stderr)
            if result.returncode == 0:
                json.loads(result.stdout)
            else:
                self.assertEqual(result.stdout, b"")

    @unittest.skipUnless(os.environ.get("PHOTOC_ARW_REAL_FIXTURE"), "optional private camera ARW not provided")
    def test_optional_real_arw(self):
        path = Path(os.environ["PHOTOC_ARW_REAL_FIXTURE"])
        self.assertTrue(path.is_file())
        data = self.exif(path)
        self.assertEqual(data["file"]["format"], "sony_arw")
        if data["camera"]["make"] is not None:
            self.assertEqual(data["camera"]["make"].upper(), "SONY")


if __name__ == "__main__":
    unittest.main()

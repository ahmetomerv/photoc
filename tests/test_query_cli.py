#!/usr/bin/env python3
"""Metadata query regressions; uses existing tiny JPEGs and generated EXIF."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get("PHOTOC_QUERY_TEST_BINARY", ROOT / "build/photoc")).resolve()
FIXTURES = ROOT / "tests/fixtures/jpeg"


def dated_jpeg(timestamp):
    # Little-endian TIFF: IFD0 -> EXIF IFD -> DateTimeOriginal ASCII payload.
    value = timestamp.encode("ascii") + b"\0"
    tiff = b"II\x2a\0" + struct.pack("<I", 8)
    tiff += struct.pack("<H", 1) + struct.pack("<HHII", 0x8769, 4, 1, 26) + b"\0" * 4
    tiff += struct.pack("<H", 1) + struct.pack("<HHII", 0x9003, 2, len(value), 44) + b"\0" * 4
    payload = b"Exif\0\0" + tiff + value
    base = (FIXTURES / "no_exif.jpg").read_bytes()
    return base[:2] + b"\xff\xe1" + struct.pack(">H", len(payload) + 2) + payload + base[2:]


class QueryCLI(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="photoc-query-")
        self.directory = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def copy(self, fixture, name):
        path = self.directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(FIXTURES / fixture, path)
        return path

    def run_query(self, *args, code=0, cwd=None):
        result = subprocess.run([str(BINARY), "query", *map(str, args)],
                                capture_output=True, cwd=cwd, timeout=20)
        self.assertEqual(result.returncode, code, result.stderr.decode(errors="replace"))
        return result

    def test_all_numeric_comparators(self):
        photo = self.copy("with_exif.jpg", "photo.jpg")
        fields = (("--iso", "200"), ("--aperture", "2.8"), ("--focal", "50"))
        for option, value in fields:
            for operator, matches in (("", True), ("=", True), (">", False),
                                      (">=", True), ("<", False), ("<=", True)):
                with self.subTest(option=option, operator=operator):
                    result = self.run_query(photo, option, operator + value)
                    self.assertEqual(result.stdout, (str(photo) + "\n").encode() if matches else b"")
            self.assertEqual(self.run_query(photo, option, ">0").stdout, (str(photo) + "\n").encode())
            self.assertEqual(self.run_query(photo, option, "<9999").stdout, (str(photo) + "\n").encode())

    def test_invalid_usage(self):
        photo = self.copy("with_exif.jpg", "photo.jpg")
        for option in ("--iso", "--aperture", "--focal"):
            for expression in ("", " ", "=", ">=", "==200", "=>200", "!=200", "> 200", "200x",
                               "nan", "inf", "0x10", "1e9999", "1e-9999", "-1"):
                with self.subTest(option=option, expression=expression):
                    self.assertEqual(self.run_query(photo, option, expression, code=2).stdout, b"")
        for args in ((), (photo, photo), (photo, "--iso"), (photo, "--camera", ""),
                     (photo, "--make", ""), (photo, "--has-gps", "--no-gps"),
                     (photo, "--json", "--print0"), (photo, "--recursive"),
                     (photo, "--iso", "1", "--iso", "2"), (photo, "--apply"),
                     (photo, "--only-errors"), (photo, "--verbose", "--quiet"),
                     (photo, "--after", "2026-02-29"), (photo, "--before", "2026-13-01"),
                     (photo, "--after", "2026-12-31", "--before", "2026-01-01")):
            self.assertEqual(self.run_query(*args, code=2).stdout, b"")
        result = subprocess.run([str(BINARY), "stats", str(self.directory), "--iso", "200"], capture_output=True)
        self.assertEqual(result.returncode, 2)

    def test_strings_and_and_filters(self):
        photo = self.copy("with_exif.jpg", "photo.jpg")
        yes = (str(photo) + "\n").encode()
        self.assertEqual(self.run_query(photo, "--camera", "Model Z").stdout, yes)
        self.assertEqual(self.run_query(photo, "--make", "Fixture Camera Co.").stdout, yes)
        for value in ("model z", "Model", "Other", "Model Z "):
            self.assertEqual(self.run_query(photo, "--camera", value).stdout, b"")
        self.assertEqual(self.run_query(photo, "--camera", "Model Z", "--make", "Fixture Camera Co.",
                                        "--iso", ">=200", "--aperture", "<=2.8", "--focal", "50",
                                        "--after", "2026-09-27", "--before", "2026-09-27", "--no-gps").stdout, yes)
        self.assertEqual(self.run_query(photo, "--camera", "Model Z", "--iso", ">200").stdout, b"")

    def test_missing_metadata_and_gps(self):
        missing = self.copy("no_exif.jpg", "missing.jpg")
        gps = self.copy("with_gps.jpeg", "gps.JPEG")
        exif = self.copy("with_exif.jpg", "exif.jpg")
        for args in (("--iso", "<100"), ("--iso", "0"), ("--aperture", "<100"),
                     ("--focal", "<100"), ("--camera", "Model Z"), ("--make", "Fixture Camera Co."),
                     ("--after", "0001-01-01"), ("--before", "9999-12-31"), ("--has-gps",)):
            self.assertEqual(self.run_query(missing, *args).stdout, b"")
        self.assertEqual(self.run_query(missing, "--no-gps").stdout, (str(missing) + "\n").encode())
        self.assertEqual(self.run_query(self.directory, "--has-gps").stdout, (str(gps) + "\n").encode())
        expected = "".join(str(p) + "\n" for p in sorted((exif, missing))).encode()
        self.assertEqual(self.run_query(self.directory, "--no-gps").stdout, expected)
        data = json.loads(self.run_query(missing, "--json").stdout)
        metadata = data["matches"][0]["metadata"]
        for key in ("iso", "aperture", "focal_length_mm", "camera", "make", "capture_timestamp", "latitude", "longitude"):
            self.assertIsNone(metadata[key])

    def test_date_boundaries(self):
        timestamps = ("2025:12:31 23:59:59", "2026:01:01 00:00:00", "2026:12:31 23:59:59",
                      "2027:01:01 00:00:00", "2026:02:30 12:00:00")
        paths = []
        for index, timestamp in enumerate(timestamps):
            path = self.directory / f"{index}.jpg"
            path.write_bytes(dated_jpeg(timestamp))
            paths.append(path)
        expected = b"".join((str(path) + "\n").encode() for path in paths[1:3])
        self.assertEqual(self.run_query(self.directory, "--after", "2026-01-01", "--before", "2026-12-31").stdout, expected)
        self.assertEqual(self.run_query(self.directory, "--after", "2026-12-31", "--before", "2026-12-31").stdout,
                         (str(paths[2]) + "\n").encode())

    def test_recursion_order_and_symlinks(self):
        a = self.copy("with_exif.jpg", "a.jpg")
        z = self.copy("with_exif.jpg", "z.JPG")
        nested = self.copy("with_exif.jpg", "nested/b.jpeg")
        (self.directory / "notes.txt").write_text("ignore")
        (self.directory / "linked.jpg").symlink_to(a)
        (self.directory / "linked-dir").symlink_to(nested.parent, target_is_directory=True)
        flat = b"".join((str(path) + "\n").encode() for path in (a, z))
        self.assertEqual(self.run_query(self.directory).stdout, flat)
        expected = b"".join((str(path) + "\n").encode() for path in (a, nested, z))
        outputs = [self.run_query(self.directory, "--recursive") for _ in range(3)]
        self.assertTrue(all(output.stdout == expected for output in outputs))
        data = json.loads(self.run_query(self.directory, "--recursive", "--json").stdout)
        self.assertEqual(data["summary"], {"files_visited": 4, "jpeg_files_found": 3, "photos_parsed": 3,
                                           "skipped_files": 1, "errors": 0, "matched": 3})
        self.assertEqual([item["path"] for item in data["matches"]], [str(a), str(nested), str(z)])

    def test_print0_and_relative_paths(self):
        paths = [self.copy("with_exif.jpg", name) for name in ("with space.jpg", "with\nnewline.jpg", "plain.jpg")]
        original = {path: (path.read_bytes(), path.stat().st_mode, path.stat().st_mtime_ns) for path in paths}
        sorted_paths = sorted(paths)
        expected = b"".join(os.fsencode(path) + b"\0" for path in sorted_paths)
        self.assertEqual(self.run_query(self.directory, "--print0").stdout, expected)
        self.assertEqual(self.run_query(self.directory).stdout,
                         b"".join(os.fsencode(path) + b"\n" for path in sorted_paths))
        data = json.loads(self.run_query(self.directory, "--json").stdout)
        self.assertEqual([item["path"] for item in data["matches"]], list(map(str, sorted_paths)))
        relative = self.run_query(".", "--print0", cwd=self.directory).stdout.split(b"\0")[:-1]
        self.assertEqual(relative, [b"./plain.jpg", b"./with\nnewline.jpg", b"./with space.jpg"])
        for path in paths:
            self.assertEqual((path.read_bytes(), path.stat().st_mode, path.stat().st_mtime_ns), original[path])

    def test_json_and_verbosity(self):
        photo = self.copy("with_gps.jpeg", "photo.jpeg")
        args = (photo, "--iso", ">=200", "--camera", "Model Z")
        baseline = self.run_query(*args, "--json")
        data = json.loads(baseline.stdout)
        self.assertEqual(set(data), {"query", "matches", "summary"})
        self.assertEqual(data["query"]["filters"]["iso"], ">=200")
        self.assertEqual(data["query"]["date_bounds"], "inclusive")
        metadata = data["matches"][0]["metadata"]
        self.assertEqual(metadata["iso"], 200)
        self.assertEqual(metadata["aperture"], 2.8)
        self.assertEqual(metadata["focal_length_mm"], 50)
        self.assertEqual(metadata["camera"], "Model Z")
        self.assertEqual(metadata["make"], "Fixture Camera Co.")
        self.assertEqual(metadata["capture_timestamp"], "2026:09:27 12:34:56")
        self.assertTrue(metadata["has_gps"])
        for option in ("--quiet", "-q", "--verbose", "-v"):
            result = self.run_query(*args, "--json", option)
            self.assertEqual(result.stdout, baseline.stdout)
        verbose = self.run_query(*args, "--verbose")
        self.assertEqual(verbose.stdout, (str(photo) + "\n").encode())
        self.assertIn(b"metadata search", verbose.stderr)
        self.assertIn(b"matched: 1", verbose.stderr)
        self.assertEqual(self.run_query(*args, "--quiet").stdout, verbose.stdout)
        self.assertEqual(self.run_query(*args, "--print0", "--verbose").stdout, os.fsencode(photo) + b"\0")

    def test_empty_results_and_failures(self):
        self.assertEqual(self.run_query(self.directory).stdout, b"")
        self.assertEqual(self.run_query(self.directory, "--print0").stdout, b"")
        self.assertEqual(json.loads(self.run_query(self.directory, "--json").stdout)["matches"], [])
        valid = self.copy("with_exif.jpg", "valid.jpg")
        invalid = self.copy("invalid.jpg", "invalid.jpg")
        for option in ("--quiet", "--verbose"):
            result = self.run_query(self.directory, "--json", option, code=1)
            data = json.loads(result.stdout)
            self.assertEqual(data["summary"]["errors"], 1)
            self.assertEqual(data["summary"]["matched"], 1)
            self.assertIn(b"invalid or truncated JPEG", result.stderr)
        self.assertEqual(self.run_query(invalid, "--json", code=1).stdout, b"")
        self.assertEqual(self.run_query(self.directory / "missing.jpg", code=1).stdout, b"")
        unsupported = self.copy("unsupported.png", "photo.png")
        self.assertEqual(self.run_query(unsupported, code=1).stdout, b"")
        (self.directory / "link.jpg").symlink_to(valid)
        self.assertEqual(self.run_query(self.directory / "link.jpg", code=1).stdout, b"")

    def test_options_before_command_and_dash_path(self):
        path = self.copy("with_exif.jpg", "-photo.jpg")
        result = subprocess.run([str(BINARY), "--iso", "200", "--json", "query", str(path)], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["summary"]["matched"], 1)
        self.assertEqual(self.run_query("--", "-photo.jpg", cwd=self.directory).stdout, b"-photo.jpg\n")


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Read-only chronology using original standard-EXIF fixtures."""
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ["PHOTOC_TIMELINE_TEST_BINARY"])
os.environ.setdefault("PHOTOC_STATS_TEST_BINARY", str(BINARY))
from test_stats_extended import tiff, BASE


class TimelineCLI(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="photoc-timeline-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()

    def photo(self, name, date=None, metadata=True, **fields):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        if path.suffix.lower() == ".arw":
            data = tiff(date=date, make="SONY", **fields)
        elif metadata:
            payload = b"Exif\0\0" + tiff(date=date, **fields)
            data = BASE[:2] + b"\xff\xe1" + struct.pack(">H", len(payload) + 2) + payload + BASE[2:]
        else:
            data = BASE
        path.write_bytes(data)
        return path

    def invoke(self, *args, code=0, env=None, cwd=None):
        result = subprocess.run([str(BINARY), *map(str, args)], capture_output=True,
                                timeout=15, env=env, cwd=cwd)
        self.assertEqual(result.returncode, code, result.stderr.decode(errors="replace"))
        return result

    def report(self, *flags):
        return json.loads(self.invoke("timeline", self.root, "--json", *flags).stdout)

    def test_multiple_dates_sessions_and_metadata(self):
        paths = [self.photo("z-last.jpg", "2026:08:24 00:00:00", model="Camera B"),
                 self.photo("a-late.jpg", "2026:08:23 09:30:01", iso=0, focal=(0, 1), aperture=(0, 1)),
                 self.photo("m-night.jpg", "2026:08:23 23:59:59", iso=1600),
                 self.photo("x-early.jpg", "2026:08:23 08:00:00", model="Camera B", iso=100, focal=(24, 1), aperture=(56, 10)),
                 self.photo("b-mid.JPG", "2026:08:23 08:30:00", model="Camera A", iso=400, focal=(9, 1), aperture=(4, 1)),
                 self.photo("missing.jpg", metadata=False),
                 self.photo("invalid-date.jpg", "2026:02:30 10:00:00")]
        before = {p: (p.read_bytes(), p.stat().st_mtime_ns, p.stat().st_mode) for p in paths}
        data = self.report()
        self.assertEqual(data["time_basis"], "recorded_local_exif")
        self.assertEqual(data["summary"]["photos_parsed"], 7)
        self.assertEqual(data["summary"]["photos_included"], 5)
        self.assertEqual(data["summary"]["photos_skipped_timestamp"], 2)
        self.assertEqual(data["summary"]["missing_timestamp"], 1)
        self.assertEqual(data["summary"]["invalid_timestamp"], 1)
        self.assertEqual(data["summary"]["sessions"], 4)
        self.assertEqual([day["date"] for day in data["dates"]], ["2026-08-23", "2026-08-24"])
        first = data["dates"][0]["sessions"][0]
        self.assertEqual(first, {"start": "08:00:00", "end": "08:30:00", "duration_seconds": 1800,
                          "photo_count": 2, "total_file_size_bytes": sum(p.stat().st_size for p in paths[3:5]),
                          "photos_with_file_size": 2, "most_used_focal_length_mm": 9,
                          "most_used_aperture": 4, "iso_min": 100, "iso_max": 400,
                          "camera_models": [{"model": "Camera A", "photo_count": 1}, {"model": "Camera B", "photo_count": 1}]})
        late = data["dates"][0]["sessions"][1]
        self.assertIsNone(late["most_used_focal_length_mm"])
        self.assertIsNone(late["most_used_aperture"])
        self.assertIsNone(late["iso_min"])
        self.assertIsNone(late["iso_max"])
        self.assertEqual(late["duration_seconds"], 0)
        self.assertEqual(before, {p: (p.read_bytes(), p.stat().st_mtime_ns, p.stat().st_mode) for p in paths})
        human = self.invoke("timeline", self.root).stdout.decode()
        self.assertIn("2026-08-23\n\n08:00:00 - 08:30:00", human)
        self.assertIn("Most-used focal length: 9 mm", human)
        self.assertIn("ISO range: 100 - 400", human)
        self.assertIn("Skipped capture timestamps: 2 (1 missing, 1 invalid)", human)
        self.assertEqual(self.report("--gap", "30m")["summary"]["sessions"], 4)
        self.assertEqual(self.report("--gap", "0m")["summary"]["sessions"], 5)
        self.assertEqual(self.report("--gap", "2h")["summary"]["sessions"], 3)
        self.assertEqual(self.report("--gap", "4294967295m")["summary"]["sessions"], 2)
        self.assertEqual(self.invoke("timeline", self.root, "--json").stdout,
                         self.invoke("timeline", self.root, "--json").stdout)
        self.assertEqual(self.report("--quiet"), data)
        self.assertEqual(self.report("--verbose"), data)
        quiet = self.invoke("timeline", self.root, "-q").stdout.decode()
        self.assertNotIn("Shooting timeline:", quiet)
        self.assertIn("Skipped capture timestamps: 2", quiet)
        for zone in ("UTC", "Pacific/Honolulu", "Europe/Berlin"):
            env = dict(os.environ, TZ=zone)
            self.assertEqual(json.loads(self.invoke("timeline", self.root, "--json", env=env).stdout), data)

    def test_exact_boundary_chained_sessions_and_midnight(self):
        self.photo("z.jpg", "2024:12:31 23:00:00")
        self.photo("y.jpg", "2024:12:31 22:00:00")
        self.photo("x.jpg", "2024:12:31 21:00:00")
        self.photo("w.jpg", "2025:01:01 00:00:00")
        data = self.report()
        self.assertEqual(len(data["dates"]), 2)
        session = data["dates"][0]["sessions"][0]
        self.assertEqual(session["photo_count"], 3)
        self.assertEqual(session["duration_seconds"], 7200)  # Gap is consecutive, not total duration.
        self.assertEqual(data["summary"]["sessions"], 2)
        self.photo("duplicate.jpg", "2024:12:31 21:00:00")
        zero = self.report("--gap", "0h")
        self.assertEqual(zero["dates"][0]["sessions"][0]["photo_count"], 2)

    def test_recursion_arw_and_metadata_failures(self):
        self.photo("one.jpg", "2026:08:23 10:00:00")
        self.photo("nested/DSC00001.ARW", "2026:08:24 11:00:00")
        (self.root / "broken.jpg").write_bytes(b"bad")
        (self.root / "notes.txt").write_text("not a photo")
        (self.root / "link.ARW").symlink_to(self.root / "nested/DSC00001.ARW")
        self.assertEqual(self.report()["summary"]["photos_included"], 1)
        recursive = self.report("--recursive")
        self.assertEqual(recursive["summary"]["photos_included"], 2)
        self.assertEqual(recursive["summary"]["metadata_errors"], 1)
        self.assertEqual(recursive["summary"]["unsupported_files"], 1)
        self.assertEqual(recursive["summary"]["files_visited"], 4)
        normal = self.invoke("timeline", self.root, "--json")
        self.assertIn(b"warning:", normal.stderr)
        quiet = self.invoke("timeline", self.root, "--json", "-q")
        self.assertEqual(quiet.stderr, b"")
        self.assertEqual(json.loads(quiet.stdout)["summary"]["metadata_errors"], 1)

    def test_empty_directory_and_no_dated_photos(self):
        data = self.report()
        self.assertEqual(data["dates"], [])
        self.assertTrue(all(n == 0 for n in data["summary"].values()))
        self.photo("plain.jpg", metadata=False)
        self.assertEqual(self.report()["dates"], [])
        self.assertEqual(self.report()["summary"]["missing_timestamp"], 1)
        self.assertIn(b"Skipped capture timestamps: 1", self.invoke("timeline", self.root).stdout)

    def test_batched_scan_and_equal_timestamp_ordering(self):
        # More than one scanner batch exercises worker loading, owned metadata,
        # photo/session array growth, and deterministic equal-time aggregation.
        cameras = ['Camera "A"', 'Camera B\\C']
        for i in range(64):
            hour = i // 4
            self.photo(f"{63 - i:03}.jpg", f"2026:08:23 {hour:02}:00:00",
                       model=cameras[i % 2], focal=(9 if i % 2 else 24, 1))
        grouped = self.report("--gap", "0m")
        self.assertEqual(grouped["summary"]["photos_included"], 64)
        self.assertEqual(grouped["summary"]["sessions"], 16)
        self.assertEqual(len(grouped["dates"]), 1)
        sessions = grouped["dates"][0]["sessions"]
        self.assertEqual([s["start"] for s in sessions],
                         [f"{hour:02}:00:00" for hour in range(16)])
        for session in sessions:
            self.assertEqual(session["photo_count"], 4)
            self.assertEqual(session["duration_seconds"], 0)
            self.assertEqual(session["most_used_focal_length_mm"], 9)
            self.assertEqual(session["camera_models"],
                             [{"model": camera, "photo_count": 2} for camera in cameras])
        self.assertEqual(self.report("--gap", "0m"), grouped)
        self.assertEqual(self.report()["summary"]["sessions"], 1)

    def test_usage_errors_and_failed_scan(self):
        invalid = [("timeline",), ("timeline", self.root, self.root),
                   ("timeline", self.root, "--gap"), ("timeline", self.root, "--gap", "bad"),
                   ("timeline", self.root, "--gap", "-1m"), ("timeline", self.root, "--gap", "4294967296m"),
                   ("timeline", self.root, "--gap", "1h", "--gap", "2h"),
                   ("timeline", self.root, "--by", "date"), ("timeline", self.root, "--apply"),
                   ("timeline", self.root, "--quiet", "--verbose")]
        for args in invalid:
            with self.subTest(args=args):
                result = self.invoke(*args, code=2)
                self.assertEqual(result.stdout, b"")
                self.assertTrue(result.stderr)
        for path in (self.root / "missing", self.photo("single.jpg", "2026:08:23 10:00:00")):
            result = self.invoke("timeline", path, "--quiet", "--json", code=1)
            self.assertEqual(result.stdout, b"")
            self.assertTrue(result.stderr)
        report = self.invoke("--json", "--gap", "30m", "timeline", self.root)
        self.assertEqual(json.loads(report.stdout)["gap_minutes"], 30)
        dash = self.root / "-outing"
        dash.mkdir()
        self.assertEqual(json.loads(self.invoke("timeline", "--json", "--", "-outing", cwd=self.root).stdout)["dates"], [])


if __name__ == "__main__":
    unittest.main()

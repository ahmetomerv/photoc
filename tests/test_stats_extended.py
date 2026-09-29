#!/usr/bin/env python3
"""Standard EXIF and additive stats contract, with original generated metadata."""
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ["PHOTOC_STATS_TEST_BINARY"])
BASE = (ROOT / "tests/fixtures/jpeg/no_exif.jpg").read_bytes()


def tiff(order="<", date=None, lens=None, equivalent=None, focal=(9, 1),
         shutter=(1, 125), orientation=1, make="Fixture", model="Model",
         iso=200, aperture=(28, 10)):
    short = lambda n: struct.pack(order + "H", n)
    rational = lambda n, d: struct.pack(order + "II", n, d)
    root = [(0x010f, 2, make.encode() + b"\0"), (0x0110, 2, model.encode() + b"\0"),
            (0x0112, 3, short(orientation)), (0x8769, 4, "exif")]
    exif = [(0x829a, 5, rational(*shutter)), (0x829d, 5, rational(*aperture)),
            (0x8827, 3, short(iso)), (0x920a, 5, rational(*focal))]
    if date is not None:
        exif.append((0x9003, 2, date.encode() + b"\0"))
    if lens is not None:
        exif.append((0xa434, 2, lens.encode() + b"\0"))
    if equivalent is not None:
        exif.append((0xa405, 3, short(equivalent)))
    root_size = 2 + 12 * len(root) + 4
    exif_offset = 8 + root_size
    value_offset = exif_offset + 2 + 12 * len(exif) + 4
    payload = bytearray()
    tables = bytearray()
    for entries in (root, exif):
        tables += short(len(entries))
        for tag, kind, value in sorted(entries):
            if value == "exif":
                value = struct.pack(order + "I", exif_offset)
            unit = {2: 1, 3: 2, 4: 4, 5: 8}[kind]
            count = len(value) // unit
            if len(value) <= 4:
                stored = value.ljust(4, b"\0")
            else:
                if (value_offset + len(payload)) % 2:
                    payload += b"\0"
                stored = struct.pack(order + "I", value_offset + len(payload))
                payload += value
            tables += struct.pack(order + "HHI", tag, kind, count) + stored
        tables += b"\0" * 4
    return (b"II" if order == "<" else b"MM") + short(42) + struct.pack(order + "I", 8) + tables + payload


class StatsExtended(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="photoc-stats-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()

    def photo(self, name="one.jpg", metadata=True, **fields):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        data = BASE
        if metadata:
            payload = b"Exif\0\0" + tiff(**fields)
            data = data[:2] + b"\xff\xe1" + struct.pack(">H", len(payload) + 2) + payload + data[2:]
        path.write_bytes(data)
        return path

    def run_cli(self, *args):
        result = subprocess.run([str(BINARY), "stats", str(self.root), *args],
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result

    def data(self, *args):
        return json.loads(self.run_cli("--json", *args).stdout)

    def test_dates_sessions_distributions_and_median(self):
        cases = [("2024:12:31 23:00:00", "Lens B", 24, (1, 125), 6),
                 ("2025:01:01 00:00:00", "Lens A", 24, (2, 250), 1),
                 ("2025:01:01 01:00:01", "Lens B", None, (1, 60), 8),
                 ("2025:02:01 12:00:00", "Lens A", None, (2, 1), 1)]
        paths = []
        for index, (date, lens, equivalent, shutter, orientation) in enumerate(cases):
            paths.append(self.photo(f"{3-index}.jpg", date=date, lens=lens,
                                    equivalent=equivalent, shutter=shutter,
                                    orientation=orientation, order="<" if index % 2 else ">"))
        paths.append(self.photo("plain.jpg", metadata=False))
        before = {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in paths}
        data = self.data()
        dist = data["distributions"]
        self.assertEqual(data["scan"]["photos_parsed"], 5)
        self.assertEqual(dist["focal_lengths_mm"][0]["value"], 9)
        self.assertEqual(dist["focal_lengths_35mm_equivalent_mm"], [{"value": 24, "count": 2, "percentage_of_photos": 40}])
        self.assertEqual([(v["value"], v["count"]) for v in dist["lens_models"]], [("Lens A", 2), ("Lens B", 2)])
        self.assertEqual(dist["shutter_speeds_seconds"][0], {"value": 0.008, "count": 2, "percentage_of_photos": 40})
        self.assertAlmostEqual(dist["shutter_speeds_seconds"][1]["value"], 1 / 60)
        self.assertEqual([(v["value"], v["count"]) for v in dist["orientations"]], [("landscape", 3), ("portrait", 2)])
        self.assertEqual(dist["resolutions"][0], {"value": "3x2", "width": 3, "height": 2,
                         "megapixels": 0.000006, "count": 5, "percentage_of_photos": 100})
        self.assertEqual([(v["value"], v["count"]) for v in dist["years"]], [("2025", 3), ("2024", 1)])
        self.assertEqual([(v["value"], v["count"]) for v in dist["months"]], [("2025-01", 2), ("2024-12", 1), ("2025-02", 1)])
        self.assertEqual(dist["days"][0]["value"], "2025-01-01")
        self.assertEqual([v["value"] for v in dist["hours"]], [0, 1, 12, 23])
        self.assertAlmostEqual(data["sessions"]["average_photos_per_session"], 4 / 3)
        sessions = dict(data["sessions"])
        sessions.pop("average_photos_per_session")
        self.assertEqual(sessions, {"gap_minutes": 60, "dated_photos": 4, "unavailable_photos": 1,
                         "count": 3,
                         "smallest_session": 1, "largest_session": 2})
        sizes = sorted(p.stat().st_size for p in paths)
        self.assertEqual(data["storage"]["median_file_size_bytes"], sizes[2])
        self.assertEqual(data["storage"]["total_bytes"], sum(sizes))
        self.assertEqual(data["storage"]["average_file_size_bytes"], sum(sizes) / 5)
        self.assertEqual(data["unavailable"]["lens_models"], 1)
        self.assertEqual(data["unavailable"]["focal_lengths_35mm_equivalent_mm"], 3)
        for group, entries in dist.items():
            self.assertEqual(sum(v["count"] for v in entries) + data["unavailable"][group], 5)
        self.assertEqual(before, {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in paths})
        text = self.run_cli().stdout
        self.assertIn("9 mm:", text)
        self.assertIn("Focal lengths (35mm equivalent)\n  24 mm:", text)
        self.assertIn("0.008 s:", text)
        self.assertIn("Median file size:", text)
        self.assertIn("Shooting sessions (gap > 60 minutes): 3", text)
        self.assertEqual(self.data("-q"), data)
        self.assertEqual(self.data("-v"), data)

    def test_zero_one_even_and_missing(self):
        empty = self.data()
        self.assertIsNone(empty["storage"]["median_file_size_bytes"])
        self.assertEqual(empty["sessions"]["count"], 0)
        self.assertIsNone(empty["sessions"]["average_photos_per_session"])
        self.assertTrue(all(not rows for rows in empty["distributions"].values()))
        one = self.photo(metadata=False)
        data = self.data()
        self.assertEqual(data["storage"]["median_file_size_bytes"], one.stat().st_size)
        self.assertEqual(data["unavailable"]["lens_models"], 1)
        self.assertEqual(data["unavailable"]["focal_lengths_35mm_equivalent_mm"], 1)
        self.assertEqual(data["sessions"]["unavailable_photos"], 1)
        two = self.photo("two.jpg", date="2026:09:30 12:00:00", lens='A "quoted" lens', equivalent=0, shutter=(1, 0))
        data = self.data()
        self.assertEqual(data["storage"]["median_file_size_bytes"], (one.stat().st_size + two.stat().st_size) / 2)
        self.assertEqual(data["sessions"]["average_photos_per_session"], 1)
        self.assertEqual(data["distributions"]["lens_models"][0]["value"], 'A "quoted" lens')
        self.assertEqual(data["distributions"]["shutter_speeds_seconds"], [])
        self.assertEqual(data["distributions"]["focal_lengths_35mm_equivalent_mm"], [])

    def test_invalid_dates_recursion_and_determinism(self):
        self.photo("bad-date.jpg", date="2025:02:29 12:00:00")
        self.photo("nested/leap.jpg", date="2024:02:29 23:59:59")
        self.assertEqual(self.data()["distributions"]["years"], [])
        first = self.run_cli("--recursive", "--json").stdout
        self.assertEqual(first, self.run_cli("--recursive", "--json").stdout)
        data = json.loads(first)
        self.assertEqual(data["distributions"]["days"][0]["value"], "2024-02-29")
        self.assertEqual(data["sessions"]["count"], 1)
        self.assertEqual(data["unavailable"]["days"], 1)

    def test_arw_standard_lens_and_equivalent(self):
        for order in ("<", ">"):
            (self.root / ("little.arw" if order == "<" else "big.ARW")).write_bytes(
                tiff(order=order, make="SONY", lens="Standard Lens", equivalent=24))
        data = self.data()
        self.assertEqual(data["scan"]["arw_files_found"], 2)
        self.assertEqual(data["distributions"]["lens_models"][0]["value"], "Standard Lens")
        self.assertEqual(data["distributions"]["focal_lengths_mm"][0]["value"], 9)
        self.assertEqual(data["distributions"]["focal_lengths_35mm_equivalent_mm"][0]["value"], 24)
        self.assertEqual(data["distributions"]["resolutions"], [])
        self.assertEqual(data["unavailable"]["orientations"], 2)

    def test_documented_schema_fields(self):
        self.photo(date="2026:09:29 12:00:00", lens="Lens", equivalent=24)
        data = self.data()
        schema = json.loads((ROOT / "docs/stats.md").read_text().split("```json\n", 1)[1].split("\n```", 1)[0])
        self.assertEqual(set(data), set(schema["properties"]))
        for key in ("scan", "storage", "capture_dates", "distributions", "sessions", "unavailable"):
            self.assertEqual(set(data[key]), set(schema["properties"][key]["properties"]))
        # Legacy types and values still appear in their original locations.
        self.assertIsInstance(data["storage"]["total_bytes"], int)
        self.assertEqual(data["capture_dates"]["earliest"], "2026:09:29 12:00:00")
        self.assertEqual(data["distributions"]["camera_models"], [{"value": "Model", "count": 1, "percentage_of_photos": 100}])
        self.assertEqual(data["distributions"]["iso"], [{"value": 200, "count": 1, "percentage_of_photos": 100}])

    def test_lens_metadata_preserves_query_ownership(self):
        path = self.photo(lens="Owned standard lens", equivalent=24)
        for source in (path, self.root):
            for flags in ((), ("--json",), ("--print0",)):
                result = subprocess.run([str(BINARY), "query", str(source), *flags],
                                        capture_output=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stderr)
                if flags == ("--json",):
                    self.assertEqual(len(json.loads(result.stdout)["matches"]), 1)
                else:
                    separator = b"\0" if flags else b"\n"
                    self.assertEqual(result.stdout, str(path).encode() + separator)


if __name__ == "__main__":
    unittest.main()

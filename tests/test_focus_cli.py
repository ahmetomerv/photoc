#!/usr/bin/env python3
"""Read-only focus reports, ordering, filtering, and failure handling."""

import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests/fixtures/jpeg"
BINARY = os.environ["PHOTOC_FOCUS_TEST_BINARY"]


class FocusTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="photoc-focus-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def copy(self, fixture, name):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(FIXTURES / fixture, path)
        return path

    def snapshot(self):
        result = {}
        for path in self.root.rglob("*"):
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode):
                contents = os.readlink(path)
            elif stat.S_ISREG(mode):
                try:
                    contents = hashlib.sha256(path.read_bytes()).hexdigest()
                except PermissionError:
                    contents = "unreadable"
            else:
                contents = None
            result[str(path.relative_to(self.root))] = (mode, contents)
        return result

    def invoke(self, *args, expected=0):
        before = self.snapshot()
        result = subprocess.run([BINARY, *map(str, args)], cwd=self.root,
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        self.assertEqual(before, self.snapshot(), "Focus changed input files")
        if expected == 0:
            self.assertEqual(result.stderr, "")
        return result

    def report(self, *args, expected=0):
        result = self.invoke(*args, expected=expected)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[:2], ["JPEG sharpness (lowest scores first)",
                                    "Filename\tSharpness score"])
        separator = lines.index("")
        rows = []
        for line in lines[2:separator]:
            fields = line.split("\t")
            self.assertIn(len(fields), (2, 3))
            rows.append((fields[0], float(fields[1]), fields[2] if len(fields) == 3 else None))
        summary = dict(line.split(": ", 1) for line in lines[separator + 1:-1])
        self.assertEqual(lines[-1], "Scores are a review aid, not proof of blur or artistic quality.")
        return rows, summary, result

    def test_single_and_default_label(self):
        path = self.copy("flat.jpg", "Photo image.JPG")
        rows, summary, _ = self.report("focus", path)
        self.assertEqual(rows, [(str(path), 0.0, "possibly blurry")])
        self.assertEqual(summary, {
            "Photos analyzed": "1", "Possibly blurry": "1 (score < 100)",
            "Files skipped": "0", "Files failed": "0",
            "Minimum score": "0.000", "Average score": "0.000", "Maximum score": "0.000"})

    def test_score_ordering_and_summary(self):
        for fixture, name in [("sharp.jpg", "a.jpg"), ("blurred.jpg", "b.jpeg"),
                              ("flat.jpg", "z.JPG")]:
            self.copy(fixture, name)
        rows, summary, result = self.report("focus", self.root)
        self.assertEqual([row[0] for row in rows], ["z.JPG", "b.jpeg", "a.jpg"])
        self.assertGreater(rows[2][1], rows[1][1])
        self.assertGreater(rows[1][1], rows[0][1])
        scores = [row[1] for row in rows]
        self.assertEqual(summary["Photos analyzed"], "3")
        self.assertAlmostEqual(float(summary["Minimum score"]), min(scores), places=3)
        self.assertAlmostEqual(float(summary["Maximum score"]), max(scores), places=3)
        self.assertLessEqual(abs(float(summary["Average score"]) - sum(scores) / 3), 0.001)
        repeated = self.invoke("focus", self.root)
        self.assertEqual(result.stdout, repeated.stdout)

    def test_ties_use_case_sensitive_path_order(self):
        for name in ["z.jpg", "a.jpg", "B.jpg"]:
            self.copy("flat.jpg", name)
        rows, _, _ = self.report("focus", self.root)
        self.assertEqual([row[0] for row in rows], ["B.jpg", "a.jpg", "z.jpg"])

    def test_larger_collection_retains_all_results(self):
        names = [f"photo-{i:03}.jpg" for i in range(70)]
        for name in reversed(names):
            self.copy("flat.jpg", name)
        rows, summary, _ = self.report("focus", self.root, "--only-blurry")
        self.assertEqual([row[0] for row in rows], names)
        self.assertEqual(summary["Photos analyzed"], "70")
        self.assertEqual(summary["Possibly blurry"], "70 (score < 100)")
        self.assertEqual(summary["Files failed"], "0")

    def test_flat_recursive_mixed_and_symlinks(self):
        source = self.copy("flat.jpg", "a.jpg")
        self.copy("sharp.jpg", "nested/b.JpEg")
        (self.root / "notes.txt").write_text("not a JPEG")
        (self.root / "link.jpg").symlink_to(source)
        (self.root / "link-dir").symlink_to(self.root / "nested", target_is_directory=True)
        rows, summary, _ = self.report("focus", self.root)
        self.assertEqual([row[0] for row in rows], ["a.jpg"])
        self.assertEqual(summary["Files skipped"], "1")
        rows, summary, _ = self.report("focus", str(self.root) + "/", "--recursive")
        self.assertEqual([row[0] for row in rows], ["a.jpg", "nested/b.JpEg"])
        self.assertEqual(summary["Photos analyzed"], "2")
        self.assertEqual(summary["Files skipped"], "1")

    def test_only_blurry_keeps_full_summary(self):
        self.copy("flat.jpg", "flat.jpg")
        self.copy("sharp.jpg", "sharp.jpg")
        rows, summary, _ = self.report("focus", self.root, "--threshold", "0.5", "--only-blurry")
        self.assertEqual([row[0] for row in rows], ["flat.jpg"])
        self.assertEqual(summary["Photos analyzed"], "2")
        self.assertEqual(summary["Possibly blurry"], "1 (score < 0.5)")
        self.assertGreater(float(summary["Maximum score"]), 0)
        self.assertGreater(float(summary["Average score"]), 0)

    def test_exact_threshold_is_not_blurry(self):
        path = self.copy("flat.jpg", "flat.jpg")
        rows, summary, _ = self.report("focus", path, "--threshold", "0")
        self.assertEqual(rows, [(str(path), 0.0, None)])
        self.assertEqual(summary["Possibly blurry"], "0 (score < 0)")
        rows, summary, _ = self.report("focus", path, "--threshold", "0", "--only-blurry")
        self.assertEqual(rows, [])
        self.assertEqual(summary["Photos analyzed"], "1")

    def test_threshold_before_command_and_scientific_notation(self):
        path = self.copy("sharp.jpg", "sharp.jpg")
        rows, summary, _ = self.report("--threshold", "1e9", "--only-blurry", "focus", path)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0][2], "possibly blurry")
        self.assertEqual(summary["Possibly blurry"], "1 (score < 1e+09)")

    def test_empty_and_no_jpegs(self):
        for has_other_file in (False, True):
            with self.subTest(has_other_file=has_other_file):
                if has_other_file:
                    (self.root / "notes.txt").write_text("text")
                rows, summary, _ = self.report("focus", self.root)
                self.assertEqual(rows, [])
                self.assertEqual(summary["Photos analyzed"], "0")
                self.assertEqual(summary["Files skipped"], str(int(has_other_file)))
                self.assertEqual(summary["Files failed"], "0")
                for key in ("Minimum score", "Average score", "Maximum score"):
                    self.assertEqual(summary[key], "Unavailable")

    def test_missing_exif_still_analyzed(self):
        path = self.copy("no_exif.jpg", "no_exif.jpg")
        rows, summary, _ = self.report("focus", path)
        self.assertEqual(len(rows), 1)
        self.assertEqual(summary["Photos analyzed"], "1")

    def test_broken_jpegs_continue_with_deterministic_warnings(self):
        self.copy("invalid.jpg", "z-broken.jpg")
        self.copy("flat.jpg", "valid.jpg")
        self.copy("invalid.jpg", "a-broken.jpg")
        rows, summary, result = self.report("focus", self.root, expected=1)
        self.assertEqual([row[0] for row in rows], ["valid.jpg"])
        self.assertEqual(summary["Photos analyzed"], "1")
        self.assertEqual(summary["Files failed"], "2")
        warnings = result.stderr.splitlines()
        self.assertEqual(len(warnings), 2)
        self.assertIn("warning: 'a-broken.jpg': image decode error:", warnings[0])
        self.assertIn("warning: 'z-broken.jpg': image decode error:", warnings[1])
        repeated = self.invoke("focus", self.root, expected=1)
        self.assertEqual(result.stdout, repeated.stdout)
        self.assertEqual(result.stderr, repeated.stderr)

    def test_invalid_single_and_disguised_non_jpeg(self):
        for fixture in ("invalid.jpg", "unsupported.png"):
            with self.subTest(fixture=fixture):
                path = self.copy(fixture, "invalid.jpg")
                rows, summary, result = self.report("focus", path, expected=1)
                self.assertEqual(rows, [])
                self.assertEqual(summary["Files failed"], "1")
                self.assertIn("image decode error:", result.stderr)

    def test_unsupported_missing_and_symlink_inputs(self):
        unsupported = self.copy("unsupported.png", "image.png")
        source = self.copy("flat.jpg", "source.jpg")
        link = self.root / "link.jpg"
        link.symlink_to(source)
        for path, message in [(unsupported, "unsupported file:"),
                              (link, "unsupported file:"),
                              (self.root / "missing.jpg", "file I/O error:")]:
            with self.subTest(path=path):
                result = self.invoke("focus", path, expected=1)
                self.assertEqual(result.stdout, "")
                self.assertIn(message, result.stderr)

    @unittest.skipIf(os.geteuid() == 0, "Root can read files without mode permissions")
    def test_unreadable_jpeg(self):
        path = self.copy("flat.jpg", "unreadable.jpg")
        path.chmod(0)
        try:
            for input_path in (self.root, path):
                with self.subTest(input_path=input_path):
                    rows, summary, result = self.report("focus", input_path, expected=1)
                    self.assertEqual(rows, [])
                    self.assertEqual(summary["Files failed"], "1")
                    self.assertIn("file I/O error:", result.stderr)
        finally:
            path.chmod(0o600)

    def test_path_beginning_with_dash(self):
        self.copy("flat.jpg", "-photo.jpg")
        rows, _, _ = self.report("focus", "--", "-photo.jpg")
        self.assertEqual(rows[0][0], "-photo.jpg")

    def test_large_jpeg_uses_existing_scaled_analysis(self):
        path = self.copy("large_sharp.jpg", "large.jpg")
        rows, summary, _ = self.report("focus", path)
        # Despite its name, the dimension-only large fixture is solid gray.
        self.assertEqual(rows[0][1], 0.0)
        self.assertEqual(summary["Photos analyzed"], "1")

    def test_usage_errors(self):
        path = self.copy("flat.jpg", "photo.jpg")
        cases = [(("focus",), "expected exactly one"),
                 (("focus", path, path), "expected exactly one"),
                 (("focus", path, "--recursive"), "--recursive requires a directory"),
                 (("focus", path, "--threshold"), "requires a value"),
                 (("focus", path, "--threshold", "--only-blurry"), "requires a value"),
                 (("focus", path, "--threshold", "1", "--threshold", "2"), "only once"),
                 (("focus", path, "--bogus"), "unknown option"),
                 (("focus", path, "--apply"), "unknown option"),
                 (("focus", "--json"), "expected exactly one"),
                 (("stats", self.root, "--threshold", "100"), "unknown option"),
                 (("exif", path, "--only-blurry"), "unknown option")]
        for args, message in cases:
            with self.subTest(args=args):
                result = self.invoke(*args, expected=2)
                self.assertEqual(result.stdout, "")
                self.assertIn(message, result.stderr)

    def test_invalid_thresholds(self):
        path = self.copy("flat.jpg", "photo.jpg")
        for value in ("", "-1", "nan", "inf", "1e9999", "1e-9999", "0x10",
                      "100px", "1.2.3", " 100", "100 "):
            with self.subTest(value=value):
                result = self.invoke("focus", path, "--threshold", value, expected=2)
                self.assertEqual(result.stdout, "")
                self.assertIn("threshold", result.stderr)

    def json_report(self, *args, expected=0):
        result = self.invoke(*args, expected=expected)
        report = json.loads(result.stdout, parse_constant=lambda value: self.fail(value))
        self.assertEqual(set(report), {"path", "threshold", "recursive", "only_blurry",
                                       "photos", "summary"})
        self.assertIsInstance(report["path"], str)
        self.assertIn(type(report["threshold"]), (int, float))
        self.assertTrue(math.isfinite(report["threshold"]))
        self.assertGreaterEqual(report["threshold"], 0)
        self.assertIs(type(report["recursive"]), bool)
        self.assertIs(type(report["only_blurry"]), bool)
        self.assertIsInstance(report["photos"], list)
        for photo in report["photos"]:
            self.assertEqual(set(photo), {"path", "score", "threshold", "possibly_blurry"})
            self.assertIsInstance(photo["path"], str)
            self.assertIn(type(photo["score"]), (int, float))
            self.assertTrue(math.isfinite(photo["score"]))
            self.assertGreaterEqual(photo["score"], 0)
            self.assertIn(type(photo["threshold"]), (int, float))
            self.assertEqual(photo["threshold"], report["threshold"])
            self.assertIs(type(photo["possibly_blurry"]), bool)
            self.assertEqual(photo["possibly_blurry"], photo["score"] < photo["threshold"])
        summary = report["summary"]
        self.assertEqual(set(summary), {"photos_analyzed", "possibly_blurry", "files_skipped",
                                        "files_failed", "minimum_score", "average_score", "maximum_score"})
        for field in ("photos_analyzed", "possibly_blurry", "files_skipped", "files_failed"):
            self.assertIs(type(summary[field]), int)
            self.assertGreaterEqual(summary[field], 0)
        for field in ("minimum_score", "average_score", "maximum_score"):
            if summary["photos_analyzed"] == 0:
                self.assertIsNone(summary[field])
            else:
                self.assertIn(type(summary[field]), (int, float))
                self.assertTrue(math.isfinite(summary[field]))
                self.assertGreaterEqual(summary[field], 0)
        self.assertLessEqual(summary["possibly_blurry"], summary["photos_analyzed"])
        self.assertLessEqual(len(report["photos"]), summary["photos_analyzed"])
        return report, result

    def test_json_single(self):
        path = self.copy("flat.jpg", "Photo image.JPG")
        report, _ = self.json_report("focus", path, "--json")
        self.assertEqual(report["path"], str(path))
        self.assertEqual(report["photos"], [{"path": str(path), "score": 0,
                                              "threshold": 100, "possibly_blurry": True}])
        self.assertEqual(report["summary"], {"photos_analyzed": 1, "possibly_blurry": 1,
                                              "files_skipped": 0, "files_failed": 0,
                                              "minimum_score": 0, "average_score": 0, "maximum_score": 0})
        self.assertFalse(report["recursive"])
        self.assertFalse(report["only_blurry"])

    def test_json_order_summary_and_determinism(self):
        for fixture, name in [("sharp.jpg", "a.jpg"), ("blurred.jpg", "b.jpeg"),
                              ("flat.jpg", "z.JPG"), ("flat.jpg", "c.jpg")]:
            self.copy(fixture, name)
        report, result = self.json_report("focus", self.root, "--json")
        self.assertEqual([photo["path"] for photo in report["photos"]],
                         ["c.jpg", "z.JPG", "b.jpeg", "a.jpg"])
        scores = [photo["score"] for photo in report["photos"]]
        summary = report["summary"]
        self.assertEqual(summary["photos_analyzed"], 4)
        self.assertEqual(summary["possibly_blurry"],
                         sum(photo["possibly_blurry"] for photo in report["photos"]))
        self.assertEqual(summary["minimum_score"], min(scores))
        self.assertEqual(summary["maximum_score"], max(scores))
        self.assertTrue(math.isclose(summary["average_score"], sum(scores) / 4, rel_tol=1e-12))
        repeated, repeated_result = self.json_report("focus", self.root, "--json")
        self.assertEqual(report, repeated)
        self.assertEqual(result.stdout, repeated_result.stdout)
        _, human, _ = self.report("focus", self.root)
        self.assertEqual(int(human["Photos analyzed"]), summary["photos_analyzed"])
        self.assertLessEqual(abs(float(human["Average score"]) - summary["average_score"]), 0.0005)

    def test_json_filter_keeps_summary(self):
        self.copy("sharp.jpg", "sharp.jpg")
        self.copy("flat.jpg", "flat.jpg")
        complete, _ = self.json_report("--json", "--threshold", "0.5", "focus", self.root)
        filtered, _ = self.json_report("focus", self.root, "--json", "--only-blurry", "--threshold", "0.5")
        self.assertEqual([photo["path"] for photo in filtered["photos"]], ["flat.jpg"])
        self.assertEqual(complete["summary"], filtered["summary"])
        self.assertEqual(filtered["summary"]["photos_analyzed"], 2)
        self.assertEqual(filtered["summary"]["possibly_blurry"], 1)
        self.assertTrue(filtered["only_blurry"])
        empty, _ = self.json_report("focus", self.root, "--json", "--only-blurry", "--threshold", "0")
        self.assertEqual(empty["photos"], [])
        self.assertEqual(empty["summary"]["photos_analyzed"], 2)
        self.assertEqual(empty["summary"]["possibly_blurry"], 0)
        self.assertGreater(empty["summary"]["maximum_score"], 0)

    def test_json_precision_at_positive_threshold_boundary(self):
        path = self.copy("sharp.jpg", "sharp.jpg")
        initial, _ = self.json_report("focus", path, "--json")
        score = initial["photos"][0]["score"]
        self.assertGreater(score, 0)
        for threshold in (math.nextafter(score, -math.inf), score, math.nextafter(score, math.inf)):
            with self.subTest(threshold=threshold):
                report, _ = self.json_report("focus", path, "--json", "--threshold", repr(threshold))
                self.assertEqual(report["threshold"], threshold)
                self.assertEqual(report["photos"][0]["score"], score)
                self.assertEqual(report["photos"][0]["possibly_blurry"], score < threshold)
                self.assertEqual(report["summary"]["possibly_blurry"], int(score < threshold))

    def test_json_recursive_mixed_and_symlinks(self):
        source = self.copy("flat.jpg", "a.jpg")
        self.copy("sharp.jpg", "nested/b.JpEg")
        (self.root / "notes.txt").write_text("not a JPEG")
        (self.root / "link.jpg").symlink_to(source)
        report, _ = self.json_report("focus", self.root, "--json")
        self.assertEqual([photo["path"] for photo in report["photos"]], ["a.jpg"])
        report, _ = self.json_report("focus", str(self.root) + "/", "--recursive", "--json")
        self.assertEqual([photo["path"] for photo in report["photos"]], ["a.jpg", "nested/b.JpEg"])
        self.assertEqual(report["summary"]["files_skipped"], 1)
        self.assertEqual(report["summary"]["photos_analyzed"], 2)
        self.assertTrue(report["recursive"])

    def test_json_empty_and_failed_scores_are_null(self):
        report, _ = self.json_report("focus", self.root, "--json")
        self.assertEqual(report["photos"], [])
        self.assertEqual(report["summary"]["photos_analyzed"], 0)
        path = self.copy("invalid.jpg", "invalid.jpg")
        report, result = self.json_report("focus", path, "--json", expected=1)
        self.assertEqual(report["photos"], [])
        self.assertEqual(report["summary"]["files_failed"], 1)
        self.assertIn("image decode error:", result.stderr)

    def test_json_partial_failure_keeps_diagnostics_on_stderr(self):
        self.copy("flat.jpg", "valid.jpg")
        self.copy("invalid.jpg", "z-broken.jpg")
        self.copy("invalid.jpg", "a-broken.jpg")
        report, result = self.json_report("focus", self.root, "--json", expected=1)
        self.assertEqual([photo["path"] for photo in report["photos"]], ["valid.jpg"])
        self.assertEqual(report["summary"]["photos_analyzed"], 1)
        self.assertEqual(report["summary"]["files_failed"], 2)
        self.assertNotIn("warning", result.stdout)
        warnings = result.stderr.splitlines()
        self.assertIn("'a-broken.jpg'", warnings[0])
        self.assertIn("'z-broken.jpg'", warnings[1])

    def test_json_escaping_and_unicode_paths(self):
        name = 'trip "Café"\\line\n\t\x01.jpg'
        path = self.copy("flat.jpg", name)
        report, result = self.json_report("focus", path, "--json")
        self.assertEqual(report["path"], str(path))
        self.assertEqual(report["photos"][0]["path"], str(path))
        for escape in ('\\"', '\\\\', '\\n', '\\t', '\\u0001'):
            self.assertIn(escape, result.stdout)
        directory_report, _ = self.json_report("focus", self.root, "--json")
        self.assertEqual(directory_report["photos"][0]["path"], next(self.root.iterdir()).name)

    def test_json_missing_metadata_and_zero_threshold(self):
        path = self.copy("no_exif.jpg", "no_exif.jpg")
        report, _ = self.json_report("focus", path, "--json", "--threshold", "0")
        self.assertEqual(len(report["photos"]), 1)
        self.assertFalse(report["photos"][0]["possibly_blurry"])
        self.assertEqual(report["summary"]["possibly_blurry"], 0)

    def test_json_fatal_and_usage_errors_have_no_report(self):
        for args, code in [(("focus", self.root / "missing.jpg", "--json"), 1),
                           (("focus", self.copy("unsupported.png", "image.png"), "--json"), 1),
                           (("focus", "--json"), 2),
                           (("focus", self.root, "--json", "--threshold", "nan"), 2),
                           (("focus", self.root, "--json", "--bogus"), 2)]:
            with self.subTest(args=args):
                result = self.invoke(*args, expected=code)
                self.assertEqual(result.stdout, "")
                self.assertTrue(result.stderr.startswith("photoc focus:"))


if __name__ == "__main__":
    unittest.main()

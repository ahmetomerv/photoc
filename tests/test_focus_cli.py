#!/usr/bin/env python3
"""Read-only focus reports, ordering, filtering, and failure handling."""

import hashlib
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
                 (("focus", path, "--json"), "--json is not supported"),
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


if __name__ == "__main__":
    unittest.main()

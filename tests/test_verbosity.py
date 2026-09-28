#!/usr/bin/env python3
"""Global output levels preserve results, JSON, failures, and file safety."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests/fixtures/jpeg"
BINARY = os.environ["PHOTOC_VERBOSITY_TEST_BINARY"]
LEVELS = ((), ("--quiet",), ("--verbose",))


class VerbosityTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="photoc-verbosity-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()

    def copy(self, fixture, name):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(FIXTURES / fixture, path)
        return path

    def invoke(self, *args, expected=0):
        if "scrub" in args:
            args = (*args, "--gps")
        result = subprocess.run([BINARY, *map(str, args)], capture_output=True,
                                text=True, cwd=self.root, timeout=10)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def unchanged(self, path):
        before = path.read_bytes()
        self.addCleanup(lambda: self.assertEqual(path.read_bytes(), before))

    def test_exif_results_and_global_routing(self):
        photo = self.copy("with_exif.jpg", "a.jpg")
        self.unchanged(photo)
        normal = self.invoke("exif", photo)
        for flag in ("-q", "--quiet", "-v", "--verbose"):
            for args in ((flag, "exif", photo), ("exif", photo, flag)):
                with self.subTest(args=args):
                    result = self.invoke(*args)
                    self.assertEqual(result.stdout, normal.stdout)
                    if "v" in flag:
                        self.assertIn("mode: metadata", result.stderr)
                        self.assertIn("metadata parse failures: 0", result.stderr)
                    else:
                        self.assertEqual(result.stderr, "")
        for command in ("--help", "--version"):
            normal = self.invoke(command)
            for level in LEVELS:
                self.assertEqual(self.invoke(*level, command).stdout, normal.stdout)

    def test_stats_retains_statistics_and_filters_noncritical_warnings(self):
        self.copy("with_exif.jpg", "a.jpg")
        self.copy("invalid.jpg", "bad.jpg")
        (self.root / "notes.txt").write_text("not a photo")
        normal = self.invoke("stats", self.root)
        self.assertIn("warning:", normal.stderr)
        self.assertIn("  Files:", normal.stdout)
        quiet = self.invoke("stats", self.root, "-q")
        self.assertEqual(quiet.stderr, "")
        self.assertNotIn("  Files:", quiet.stdout)
        self.assertNotIn("Directory:", quiet.stdout)
        # Everything from the actual statistics report remains unchanged.
        result_lines = [line for line in normal.stdout.splitlines()
                        if not line.startswith(("  Directory:", "  Scan:", "  Files:"))]
        self.assertEqual(quiet.stdout.splitlines(), result_lines)
        verbose = self.invoke("stats", self.root, "-v")
        self.assertEqual(verbose.stdout, normal.stdout)
        for diagnostic in ("files scanned: 3", "JPEG files discovered: 2",
                           "skipped: 2", "metadata parse failures: 1", "worker limit:"):
            self.assertIn(diagnostic, verbose.stderr)

    def test_duplicates_keeps_groups_and_savings(self):
        first = self.copy("with_exif.jpg", "a.jpg")
        self.copy("with_exif.jpg", "b.jpg")
        self.unchanged(first)
        normal = self.invoke("duplicates", self.root)
        quiet = self.invoke("duplicates", self.root, "-q")
        self.assertIn("a.jpg", quiet.stdout)
        self.assertIn("b.jpg", quiet.stdout)
        self.assertIn("Total duplicate groups: 1", quiet.stdout)
        self.assertIn("Potential storage savings:", quiet.stdout)
        self.assertNotIn("Directory:", quiet.stdout)
        self.assertNotIn("Files scanned:", quiet.stdout)
        self.assertEqual(quiet.stderr, "")
        verbose = self.invoke("duplicates", self.root, "-v")
        self.assertEqual(verbose.stdout, normal.stdout)
        self.assertIn("files scanned: 2", verbose.stderr)
        self.assertIn("duplicate groups: 1", verbose.stderr)
        self.assertIn("worker limit:", verbose.stderr)

    def test_focus_keeps_scores_and_failure_explanations(self):
        self.copy("flat.jpg", "flat.jpg")
        self.copy("invalid.jpg", "bad.jpg")
        (self.root / "notes.txt").write_text("skip")
        normal = self.invoke("focus", self.root, expected=1)
        quiet = self.invoke("focus", self.root, "--quiet", expected=1)
        self.assertEqual(normal.stderr, quiet.stderr)
        self.assertIn("warning:", quiet.stderr)
        self.assertIn("image decode error", quiet.stderr)
        self.assertIn("flat.jpg\t0.000", quiet.stdout)
        self.assertNotIn("Files skipped:", quiet.stdout)
        self.assertNotIn("review aid", quiet.stdout)
        verbose = self.invoke("focus", self.root, "-v", expected=1)
        self.assertEqual(normal.stdout, verbose.stdout)
        self.assertIn("decode failures: 1", verbose.stderr)
        self.assertIn("unsupported file extension", verbose.stderr)

    def test_json_reports_are_identical_in_all_modes(self):
        photo = self.copy("flat.jpg", "a.jpg")
        self.copy("flat.jpg", "b.jpg")
        self.copy("invalid.jpg", "bad.jpg")
        for command, path, expected in (("exif", photo, 0), ("stats", self.root, 0),
                                        ("duplicates", self.root, 0),
                                        ("focus", self.root, 1)):
            with self.subTest(command=command):
                baseline = self.invoke(command, path, "--json", expected=expected)
                parsed = json.loads(baseline.stdout)
                self.assertIsInstance(parsed, dict)
                for level in LEVELS[1:]:
                    result = self.invoke(*level, command, path, "--json", expected=expected)
                    self.assertEqual(result.stdout, baseline.stdout)
                    self.assertEqual(json.loads(result.stdout), parsed)
                    if level == ("--verbose",):
                        self.assertIn(f"photoc {command}: verbose:", result.stderr)
                    elif command == "stats":
                        self.assertEqual(result.stderr, "")
                    elif expected:
                        self.assertEqual(result.stderr, baseline.stderr)
        for command in ("exif", "stats", "duplicates", "focus"):
            result = self.invoke(command, self.root / "missing", "--json", "-q", expected=1)
            self.assertEqual(result.stdout, "")
            self.assertIn(f"photoc {command}:", result.stderr)

    def test_compress_and_scrub_status_and_paths(self):
        for command, fixture, suffix in (("compress", "with_exif.jpg", "compressed"),
                                          ("scrub", "with_gps.jpeg", "scrubbed")):
            output_hashes = []
            for index, level in enumerate(LEVELS):
                with self.subTest(command=command, level=level):
                    photo = self.copy(fixture, f"{command}/{index}/a.jpg")
                    before = photo.read_bytes()
                    result = self.invoke(*level, command, photo)
                    destination = photo.with_name(f"a.{suffix}.jpg")
                    self.assertTrue(destination.is_file())
                    self.assertEqual(photo.read_bytes(), before)
                    output_hashes.append(hashlib.sha256(destination.read_bytes()).hexdigest())
                    if level == ("--quiet",):
                        self.assertEqual(result.stdout, "")
                        self.assertEqual(result.stderr, "")
                    else:
                        self.assertIn(str(destination), result.stdout)
                    if level == ("--verbose",):
                        self.assertIn("mode:", result.stderr)
                        self.assertIn(f"input: '{photo}'; output: '{destination}'", result.stderr)
                    collision = self.invoke(command, photo, "-q", expected=1)
                    self.assertEqual(collision.stdout, "")
                    self.assertIn("collision", collision.stderr)
            self.assertEqual(len(set(output_hashes)), 1)

    def test_batch_modifications_and_skip_diagnostics(self):
        for command, fixture in (("compress", "flat.jpg"), ("scrub", "with_gps.jpeg")):
            for index, level in enumerate(LEVELS):
                photo = self.copy(fixture, f"{command}/{index}/a.jpg")
                folder = photo.parent
                (folder / "skip.txt").write_text("skip")
                self.copy(fixture, f"{command}/{index}/nested/b.jpg")
                result = self.invoke(command, folder, "--recursive", *level)
                if level == ("--quiet",):
                    self.assertEqual(result.stdout, "")
                else:
                    self.assertIn("Files processed: 2", result.stdout)
                if level == ("--verbose",):
                    self.assertIn("recursion: enabled", result.stderr)
                    self.assertIn("JPEG files discovered: 2", result.stderr)
                    self.assertIn("skipped: 1", result.stderr)
                for invalid in ("missing.jpg", "bad.jpg"):
                    if invalid == "bad.jpg":
                        self.copy("invalid.jpg", f"{command}/{index}/bad.jpg")
                    failed = self.invoke(command, folder / invalid, "-q", expected=1)
                    self.assertEqual(failed.stdout, "")
                    self.assertIn(f"photoc {command}:", failed.stderr)

    def test_scrub_in_place_and_no_gps_quiet(self):
        photo = self.copy("with_gps.jpeg", "gps.jpg")
        before = photo.read_bytes()
        result = self.invoke("scrub", photo, "--in-place", "-v")
        self.assertIn("mode: in place", result.stderr)
        self.assertNotEqual(photo.read_bytes(), before)
        self.assertEqual(self.invoke("scrub", photo, "-q").stdout, "")
        self.assertEqual(self.invoke("scrub", photo, "-q").stderr, "")

    def test_unreachable_target_remains_a_failure_in_quiet(self):
        photo = self.copy("with_exif.jpg", "a.jpg")
        result = self.invoke("compress", photo, "--target", "1", "-q", expected=1)
        self.assertEqual(result.stdout, "")
        self.assertIn("target cannot be reached", result.stderr)
        self.assertTrue(photo.with_name("a.compressed.jpg").is_file())

    def test_rename_sort_plans_and_apply(self):
        for command in ("rename", "sort"):
            for index, level in enumerate(LEVELS):
                with self.subTest(command=command, level=level):
                    photo = self.copy("with_exif.jpg", f"{command}/{index}/a.jpg")
                    before = photo.read_bytes()
                    (photo.parent / "skip.txt").write_text("skip")
                    options = ("--format", "renamed.{ext}") if command == "rename" else ("--by", "date")
                    preview = self.invoke(command, photo.parent, *options, *level)
                    self.assertIn("a.jpg ->", preview.stdout)
                    self.assertEqual(photo.read_bytes(), before)
                    if level == ("--quiet",):
                        self.assertNotIn("Summary:", preview.stdout)
                    else:
                        self.assertIn("Summary:", preview.stdout)
                    if level == ("--verbose",):
                        self.assertIn("preview", preview.stderr)
                        self.assertIn("skipped: 1", preview.stderr)
                        self.assertIn("JPEG files discovered: 1", preview.stderr)
                    applied = self.invoke(command, photo.parent, *options, "--apply", *level)
                    self.assertIn("a.jpg ->", applied.stdout)
                    self.assertFalse(photo.exists())
                    destination = photo.parent / ("renamed.jpg" if command == "rename"
                                                   else "2026/09/27/a.jpg")
                    self.assertEqual(destination.read_bytes(), before)
                    if level == ("--verbose",):
                        self.assertIn("apply", applied.stderr)
                        self.assertIn("applied: 1", applied.stderr)

    def test_quiet_blocked_plans_keep_errors(self):
        for command in ("rename", "sort"):
            for apply in ((), ("--apply",)):
                photo = self.copy("invalid.jpg", f"{command}/{len(apply)}/bad.jpg")
                before = photo.read_bytes()
                options = ("--format", "renamed.{ext}") if command == "rename" else ("--by", "date")
                result = self.invoke(command, photo.parent, *options, *apply, "-q", expected=1)
                self.assertIn("image decode error", result.stderr)
                self.assertNotIn("Summary:", result.stdout)
                self.assertEqual(photo.read_bytes(), before)
                if apply:
                    self.assertIn("no files changed", result.stderr)

    @unittest.skipIf(os.geteuid() == 0, "root can read mode-000 files")
    def test_duplicates_quiet_keeps_hash_failure_warnings(self):
        first = self.copy("with_exif.jpg", "a.jpg")
        self.copy("with_exif.jpg", "b.jpg")
        first.chmod(0)
        self.addCleanup(lambda: first.chmod(0o600))
        normal = self.invoke("duplicates", self.root, expected=1)
        quiet = self.invoke("duplicates", self.root, "-q", expected=1)
        self.assertEqual(normal.stderr, quiet.stderr)
        self.assertIn("warning:", quiet.stderr)
        self.assertIn("Permission denied", quiet.stderr)

    def test_conflicting_options_are_usage_errors(self):
        for command in ("exif", "stats", "duplicates", "focus", "compress", "scrub", "rename", "sort"):
            for flags in (("-q", "-v"), ("--verbose", "--quiet"), ("--quiet", "-v")):
                result = self.invoke(flags[0], command, self.root, flags[1], expected=2)
                self.assertEqual(result.stdout, "")
                self.assertEqual(result.stderr, "photoc: --verbose and --quiet cannot be used together\n")


if __name__ == "__main__":
    unittest.main()

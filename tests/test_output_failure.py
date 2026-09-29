#!/usr/bin/env python3
"""Buffered stdout failures must not report successful CLI output."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BINARY = os.environ["PHOTOC_OUTPUT_TEST_BINARY"]


class OutputFailureTests(unittest.TestCase):
    def test_readonly_output_is_reported(self):
        cases = (
            ("--version",),
            ("--help",),
            ("exif", "--help"),
            ("exif", str(ROOT / "tests/fixtures/jpeg/with_exif.jpg"), "--json"),
            ("stats", str(ROOT / "tests/fixtures/jpeg"), "--json"),
            ("timeline", str(ROOT / "tests/fixtures/jpeg")),
            ("timeline", str(ROOT / "tests/fixtures/jpeg"), "--json"),
            ("focus", str(ROOT / "tests/fixtures/jpeg/flat.jpg")),
            ("focus", str(ROOT / "tests/fixtures/jpeg/flat.jpg"), "--json"),
            ("query", str(ROOT / "tests/fixtures/jpeg/flat.jpg")),
            ("query", str(ROOT / "tests/fixtures/jpeg/flat.jpg"), "--json"),
            ("query", str(ROOT / "tests/fixtures/jpeg/flat.jpg"), "--print0"),
            ("check", str(ROOT / "tests/fixtures/jpeg/flat.jpg")),
            ("check", str(ROOT / "tests/fixtures/jpeg/flat.jpg"), "--json"),
        )
        with tempfile.TemporaryDirectory(prefix="photoc-output-test-") as temporary:
            path = Path(temporary) / "readonly"
            original = b"Existing file must remain unchanged.\n"
            path.write_bytes(original)
            cases = tuple(args + (flag,) for args in cases
                          for flag in ("--quiet", "--verbose")) + cases
            for args in cases:
                with self.subTest(args=args), path.open("rb") as output:
                    result = subprocess.run([BINARY, *args], stdout=output,
                                            stderr=subprocess.PIPE, text=True)
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertIn("standard output", result.stderr)
                    self.assertEqual(path.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Validate tap formula generation without a published tag or Homebrew installation."""

import contextlib
import hashlib
import importlib.util
import io
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("package_release", ROOT / "scripts/package-release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class HomebrewFormulaTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="photoc-homebrew-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.archive = self.root / "source.tar.gz"
        self.formula = self.root / "Formula/photoc.rb"
        self.version = release.project_version()

    def source_archive(self, version=None, omit=None):
        with tarfile.open(self.archive, "w:gz") as archive:
            contents = ((version if version is not None else self.version) + "\n").encode("ascii")
            member = tarfile.TarInfo("photoc-source/VERSION")
            member.size = len(contents)
            archive.addfile(member, io.BytesIO(contents))
            for name in ("CMakeLists.txt", "cmake/Version.cmake", "src/main.c", "man/photoc.1",
                         "completions/bash/photoc", "completions/zsh/_photoc",
                         "completions/fish/photoc.fish"):
                if name != omit:
                    archive.add(ROOT / name, arcname="photoc-source/" + name)

    def generate(self):
        with contextlib.redirect_stdout(io.StringIO()):
            release.homebrew_formula(self.archive, self.formula)

    def test_formula_pins_tagged_source_with_actual_archive_checksum(self):
        self.source_archive()
        self.generate()
        formula = self.formula.read_text()
        digest = hashlib.sha256(self.archive.read_bytes()).hexdigest()
        self.assertIn(f'url "https://github.com/ahmetomerv/photoc/archive/refs/tags/v{self.version}.tar.gz"', formula)
        self.assertIn(f'sha256 "{digest}"', formula)
        self.assertNotIn("@VERSION@", formula)
        self.assertNotIn("@SHA256@", formula)

    def test_mismatched_source_version_is_rejected(self):
        self.source_archive(version=self.version + "-wrong")
        with self.assertRaisesRegex(ValueError, "VERSION does not match"):
            self.generate()
        self.assertFalse(self.formula.exists())

    def test_missing_source_files_are_rejected(self):
        self.source_archive(omit="CMakeLists.txt")
        with self.assertRaisesRegex(ValueError, "missing CMakeLists.txt"):
            self.generate()
        self.assertFalse(self.formula.exists())

    def test_archive_without_version_is_rejected(self):
        with tarfile.open(self.archive, "w:gz"):
            pass
        with self.assertRaisesRegex(ValueError, "top-level VERSION"):
            self.generate()
        self.assertFalse(self.formula.exists())

    def test_existing_formula_is_preserved(self):
        self.source_archive()
        self.formula.parent.mkdir()
        self.formula.write_text("maintainer changes\n")
        with self.assertRaises(FileExistsError):
            self.generate()
        self.assertEqual(self.formula.read_text(), "maintainer changes\n")

    def test_invalid_archive_reports_cli_error_without_output_file(self):
        self.archive.write_text("invalid archive")
        result = subprocess.run(
            [sys.executable, str(ROOT / "scripts/package-release.py"), "homebrew",
             "--source-archive", str(self.archive), "--output", str(self.formula)],
            capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 1)
        self.assertIn("release: error:", result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertFalse(self.formula.exists())

    def test_generated_formula_is_valid_ruby(self):
        ruby = shutil.which("ruby")
        if not ruby:
            self.skipTest("Ruby is unavailable")
        self.source_archive()
        self.generate()
        result = subprocess.run([ruby, "-c", str(self.formula)],
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()

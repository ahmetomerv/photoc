#!/usr/bin/env python3
"""Check the canonical version, release gates, and CMake regeneration in isolation."""

import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("package_release", ROOT / "scripts/package-release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)
BINARY = Path(os.environ.get("PHOTOC_VERSION_TEST_BINARY", str(ROOT / "build/photoc")))
CMAKE = os.environ.get("PHOTOC_VERSION_TEST_CMAKE", "cmake")


class VersioningTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="photoc-version-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / "cmake").mkdir()
        shutil.copyfile(ROOT / "cmake/Version.cmake", self.root / "cmake/Version.cmake")
        shutil.copyfile(ROOT / "include/photoc/version.h.in", self.root / "version.h.in")

    def project(self, language="NONE"):
        contents = (
            'cmake_minimum_required(VERSION 3.21)\n'
            'include(cmake/Version.cmake)\n'
            f'project(version_probe VERSION "${{PHOTOC_VERSION}}" LANGUAGES {language})\n'
            'configure_file(version.h.in version.h @ONLY)\n'
        )
        if language == "C":
            contents += (
                'set(CMAKE_C_STANDARD 17)\n'
                'set(CMAKE_C_STANDARD_REQUIRED ON)\n'
                'add_executable(version_probe main.c)\n'
                'target_include_directories(version_probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")\n'
            )
            (self.root / "main.c").write_text(
                '#include <stdio.h>\n#include "version.h"\n'
                'int main(void) { puts("photoc " PHOTOC_VERSION); return 0; }\n',
                encoding="ascii")
        (self.root / "CMakeLists.txt").write_text(contents, encoding="ascii")

    def run_tool(self, *command):
        return subprocess.run(command, capture_output=True, text=True, timeout=30)

    def configure(self, build):
        command = [CMAKE, "-S", str(self.root), "-B", str(build)]
        compiler = os.environ.get("PHOTOC_VERSION_TEST_COMPILER")
        if compiler:
            command.append(f"-DCMAKE_C_COMPILER={compiler}")
        return self.run_tool(*command)

    def assert_success(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_cli_and_release_tool_read_canonical_version(self):
        canonical = (ROOT / "VERSION").read_text(encoding="ascii").strip()
        self.assertEqual(release.project_version(), canonical)
        result = self.run_tool(str(BINARY), "--version")
        self.assert_success(result)
        self.assertEqual(result.stdout, f"photoc {canonical}\n")
        self.assertEqual(result.stderr, "")
        for arguments in ((), ("--tag", "v" + canonical)):
            result = self.run_tool(
                sys.executable, str(ROOT / "scripts/package-release.py"), "version", *arguments)
            self.assert_success(result)
            self.assertEqual(result.stdout, canonical + "\n")
            self.assertEqual(result.stderr, "")

    def test_release_tag_mismatch_is_a_clear_error(self):
        result = self.run_tool(
            sys.executable, str(ROOT / "scripts/package-release.py"), "version", "--tag", "wrong")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")
        self.assertIn("does not match VERSION", result.stderr)
        self.assertIn("expected 'v" + release.project_version() + "'", result.stderr)

    def test_cmake_and_release_tool_agree_on_valid_versions(self):
        self.project()
        for version in ("0.0.0", "1.2.3", "12.34.56"):
            with self.subTest(version=version):
                (self.root / "VERSION").write_text(version + "\n", encoding="ascii")
                with mock.patch.object(release, "ROOT", self.root):
                    self.assertEqual(release.release_version("v" + version), version)
                build = self.root / ("build-" + version)
                self.assert_success(self.configure(build))
                self.assertIn(f'#define PHOTOC_VERSION "{version}"', (build / "version.h").read_text())

    def test_invalid_versions_are_rejected_by_both_readers(self):
        self.project()
        invalid = ("", "1", "1.2", "1.2.3.4", "01.2.3", "1.02.3", "1.2.03",
                   "-1.2.3", "v1.2.3", "1.2.3-rc.1", "1.2.3+build.1", "1.2.3\n4.5.6")
        for index, version in enumerate(invalid):
            with self.subTest(version=version):
                (self.root / "VERSION").write_text(version + "\n", encoding="ascii")
                with mock.patch.object(release, "ROOT", self.root):
                    with self.assertRaisesRegex(ValueError, "VERSION must contain"):
                        release.project_version()
                result = self.configure(self.root / f"invalid-{index}")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("VERSION must contain", result.stderr)

    def test_missing_canonical_source_fails(self):
        self.project()
        with mock.patch.object(release, "ROOT", self.root):
            with self.assertRaises(FileNotFoundError):
                release.project_version()
        result = self.configure(self.root / "missing")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("VERSION", result.stderr)

    def test_build_regenerates_version_when_source_changes(self):
        self.project("C")
        (self.root / "VERSION").write_text("1.2.3\n", encoding="ascii")
        build = self.root / "build"
        self.assert_success(self.configure(build))
        self.assert_success(self.run_tool(CMAKE, "--build", str(build)))
        self.assertEqual(self.run_tool(str(build / "version_probe")).stdout, "photoc 1.2.3\n")
        # Give generators with whole-second timestamps an observable change.
        time.sleep(1.1)
        (self.root / "VERSION").write_text("1.2.4\n", encoding="ascii")
        self.assert_success(self.run_tool(CMAKE, "--build", str(build)))
        self.assertEqual(self.run_tool(str(build / "version_probe")).stdout, "photoc 1.2.4\n")


if __name__ == "__main__":
    unittest.main()

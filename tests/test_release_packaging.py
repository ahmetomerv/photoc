#!/usr/bin/env python3
"""Exercise release archives and checksum gates with the actual built executable."""

import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("package_release", ROOT / "scripts/package-release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)
BINARY = Path(os.environ.get("PHOTOC_RELEASE_TEST_BINARY", str(ROOT / "build/photoc")))


class ReleasePackagingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="photoc-release-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.output = self.root / "dist with spaces"
        version = subprocess.check_output([str(BINARY), "--version"], text=True).strip().split()[1]
        self.tag = "v" + version
        self.platform = "darwin-arm64"

    def package(self, platform=None, output=None):
        with contextlib.redirect_stdout(io.StringIO()):
            release.package(self.tag, platform or self.platform, BINARY, output or self.output)

    def package_with_metadata(self, platform=None, output=None, binary_digest=None):
        platform = platform or self.platform
        dependency_manifest = self.root / f"{platform}-dependencies-input.json"
        sbom = self.root / f"{platform}-sbom-input.json"
        dependency_manifest.write_text(
            json.dumps({
                "schemaVersion": 1,
                "release": {"tag": self.tag, "platform": platform},
                "build": {"binary": {"sha256": binary_digest or release.sha256(BINARY)}},
            }),
            encoding="utf-8",
        )
        sbom.write_text(
            json.dumps({"spdxVersion": "SPDX-2.3", "SPDXID": "SPDXRef-DOCUMENT"}),
            encoding="utf-8",
        )
        with contextlib.redirect_stdout(io.StringIO()):
            release.package(
                self.tag, platform, BINARY, output or self.output,
                dependency_manifest=dependency_manifest, sbom=sbom,
            )

    def all_platforms(self):
        for platform in release.PLATFORMS:
            self.package(platform)

    def collect(self):
        with contextlib.redirect_stdout(io.StringIO()):
            release.collect_checksums(self.tag, self.output)

    def test_archive_contains_executable_docs_and_license(self):
        self.package()
        raw, archive, checksums = release.asset_names(self.tag, self.platform)
        self.assertEqual(set(path.name for path in self.output.iterdir()), {raw, archive, checksums})
        self.assertEqual((self.output / raw).read_bytes(), BINARY.read_bytes())
        self.assertEqual((self.output / raw).stat().st_mode & 0o777, 0o755)
        base = archive.removesuffix(".tar.gz")
        with tarfile.open(self.output / archive) as bundle:
            for name in ("bin/photoc", "VERSION", "LICENSE", "THIRD_PARTY_NOTICES.md",
                         "CODE_OF_CONDUCT.md", "SECURITY.md",
                         "licenses/libexif/COPYING", "licenses/libjpeg-turbo/README.ijg",
                         "licenses/libjpeg-turbo/LICENSE-3.2.0.md",
                         "licenses/libjpeg-turbo/LICENSE-2.1.2.md",
                         "README.md", "assets/banner.png", "assets/photoc.svg",
                         "share/man/man1/photoc.1",
                         "completions/bash/photoc", "completions/zsh/_photoc",
                         "completions/fish/photoc.fish", "docs/exif.md", "scripts/uninstall.sh"):
                self.assertTrue(bundle.getmember(base + "/" + name).isfile(), name)
                if name == "THIRD_PARTY_NOTICES.md" or name.startswith("licenses/"):
                    self.assertEqual(bundle.extractfile(base + "/" + name).read(),
                                     (ROOT / name).read_bytes())
            self.assertEqual(bundle.extractfile(base + "/VERSION").read(), (ROOT / "VERSION").read_bytes())
            binary_member = bundle.getmember(base + "/bin/photoc")
            self.assertEqual(binary_member.mode, 0o755)
            self.assertTrue(all(member.uid == 0 and member.gid == 0 and member.mtime == 0
                                for member in bundle.getmembers()))
            extracted = self.root / "extracted-photoc"
            extracted.write_bytes(bundle.extractfile(binary_member).read())
            extracted.chmod(0o755)
        output = subprocess.check_output([str(extracted), "--version"], text=True)
        self.assertEqual(output, f"photoc {self.tag[1:]}\n")

    def test_archive_metadata_is_deterministic(self):
        self.package()
        another = self.root / "other output"
        self.package(output=another)
        for name in release.asset_names(self.tag, self.platform):
            self.assertEqual((self.output / name).read_bytes(), (another / name).read_bytes())

    def test_dependency_manifest_and_sbom_are_release_assets_with_checksums(self):
        self.package_with_metadata()
        raw, archive, checksums = release.asset_names(self.tag, self.platform)
        dependencies, sbom = release.metadata_names(self.tag, self.platform)
        self.assertEqual(
            set(path.name for path in self.output.iterdir()),
            {raw, archive, checksums, dependencies, sbom},
        )
        checksum_names = {
            line.split("  ", 1)[1]
            for line in (self.output / checksums).read_text(encoding="ascii").splitlines()
        }
        self.assertEqual(checksum_names, {raw, archive, dependencies, sbom})
        self.assertEqual(
            json.loads((self.output / dependencies).read_text(encoding="utf-8"))["schemaVersion"],
            1,
        )
        self.assertEqual(
            json.loads((self.output / sbom).read_text(encoding="utf-8"))["spdxVersion"],
            "SPDX-2.3",
        )

    def test_dependency_manifest_must_match_packaged_binary(self):
        with self.assertRaisesRegex(ValueError, "binary digest"):
            self.package_with_metadata(binary_digest="0" * 64)
        self.assertFalse(self.output.exists())

    def test_tag_validation(self):
        for tag in ("0.1.0", "v1.2", "v1.2.3-rc.1", "v01.2.3", "v1.2.3/../bad"):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                release.package(tag, self.platform, BINARY, self.output)
        self.assertFalse(self.output.exists())

    def test_tag_must_match_canonical_version(self):
        major, minor, patch = map(int, self.tag[1:].split("."))
        wrong = f"v{major}.{minor}.{patch + 1}"
        with self.assertRaisesRegex(ValueError, "does not match VERSION"):
            release.package(wrong, self.platform, BINARY, self.output)
        self.assertFalse(self.output.exists())

    def test_stale_binary_is_rejected_before_packaging(self):
        major, minor, patch = map(int, self.tag[1:].split("."))
        version = f"{major}.{minor}.{patch + 1}"
        (self.root / "VERSION").write_text(version + "\n", encoding="ascii")
        with mock.patch.object(release, "ROOT", self.root):
            with self.assertRaisesRegex(ValueError, "does not match binary version"):
                release.package("v" + version, self.platform, BINARY, self.output)
        self.assertFalse(self.output.exists())

    def test_existing_asset_is_preserved(self):
        raw, _, _ = release.asset_names(self.tag, self.platform)
        self.output.mkdir()
        protected = self.output / raw
        protected.write_text("user data")
        with self.assertRaisesRegex(ValueError, "refusing to replace"):
            self.package()
        self.assertEqual(protected.read_text(), "user data")
        self.assertEqual(len(list(self.output.iterdir())), 1)

    def test_combined_checksums_verify_with_system_tool(self):
        self.all_platforms()
        self.collect()
        lines = (self.output / "SHA256SUMS").read_text().splitlines()
        self.assertEqual(len(lines), 6)
        names = [line.split("  ", 1)[1] for line in lines]
        self.assertEqual(names, sorted(names))
        tool = shutil.which("sha256sum")
        if tool:
            command = [tool, "-c", "SHA256SUMS"]
        else:
            tool = shutil.which("shasum")
            if not tool:
                self.skipTest("neither sha256sum nor shasum is available")
            command = [tool, "-a", "256", "-c", "SHA256SUMS"]
        result = subprocess.run(command, cwd=self.output, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_missing_platform_blocks_manifest(self):
        self.package()
        with self.assertRaises(OSError):
            self.collect()
        self.assertFalse((self.output / "SHA256SUMS").exists())

    def test_corruption_blocks_manifest(self):
        self.all_platforms()
        raw, _, _ = release.asset_names(self.tag, self.platform)
        (self.output / raw).write_bytes(b"corrupted")
        with self.assertRaisesRegex(ValueError, "verification failed"):
            self.collect()
        self.assertFalse((self.output / "SHA256SUMS").exists())

    def test_unexpected_or_duplicate_manifest_entries_are_rejected(self):
        self.all_platforms()
        raw, _, manifest = release.asset_names(self.tag, self.platform)
        record = self.output / manifest
        original = record.read_text()
        for contents in (original + original, "", f"{'0' * 64}  ../../unrelated\n"):
            with self.subTest(contents=contents):
                record.write_text(contents)
                with self.assertRaises(ValueError):
                    self.collect()
                self.assertFalse((self.output / "SHA256SUMS").exists())
        record.write_text(original)

    def test_symlink_asset_is_rejected(self):
        self.all_platforms()
        raw, _, _ = release.asset_names(self.tag, self.platform)
        (self.output / raw).unlink()
        (self.output / raw).symlink_to(BINARY.resolve())
        with self.assertRaisesRegex(ValueError, "verification failed"):
            self.collect()
        self.assertFalse((self.output / "SHA256SUMS").exists())

    def test_existing_combined_manifest_is_preserved(self):
        self.all_platforms()
        manifest = self.output / "SHA256SUMS"
        manifest.write_text("existing manifest")
        with self.assertRaises(FileExistsError):
            self.collect()
        self.assertEqual(manifest.read_text(), "existing manifest")

    def test_combined_checksums_can_require_dependency_metadata(self):
        for platform in release.PLATFORMS:
            self.package_with_metadata(platform)
        with contextlib.redirect_stdout(io.StringIO()):
            release.collect_checksums(self.tag, self.output, require_metadata=True)
        lines = (self.output / "SHA256SUMS").read_text(encoding="ascii").splitlines()
        self.assertEqual(len(lines), 12)

    def test_required_dependency_metadata_blocks_incomplete_release(self):
        self.all_platforms()
        with self.assertRaisesRegex(ValueError, "dependency manifest and SBOM"):
            release.collect_checksums(self.tag, self.output, require_metadata=True)
        self.assertFalse((self.output / "SHA256SUMS").exists())


class ReleaseDependencyManifestTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="photoc-provenance-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / "build"
        compiler_config = self.build / "CMakeFiles/4.4.4/CMakeCCompiler.cmake"
        compiler_config.parent.mkdir(parents=True)
        compiler_config.write_text(
            'set(CMAKE_C_COMPILER "/usr/bin/cc")\n'
            'set(CMAKE_C_COMPILER_ID "GNU")\n'
            'set(CMAKE_C_COMPILER_VERSION "13.3.0")\n',
            encoding="utf-8",
        )
        (self.build / "CMakeCache.txt").write_text(
            "CMAKE_COMMAND:INTERNAL=/usr/bin/cmake\n", encoding="utf-8"
        )
        self.binary = self.build / "photoc"
        self.binary.write_bytes(b"test binary")
        self.workflow = self.root / "release.yml"
        self.workflow.write_text(
            "steps:\n"
            "  - uses: actions/checkout@" + "a" * 40 + " # v7.0.1\n"
            "  - uses: actions/attest@" + "b" * 40 + " # v4.2.2\n",
            encoding="utf-8",
        )

    def test_manifest_records_source_toolchain_packages_libraries_and_actions(self):
        pkg_modules = {
            "libexif": ("0.6.26", "/usr", "/usr/lib", "/usr/include", "/usr/lib/pkgconfig"),
            "libturbojpeg": ("3.2.0", "/usr", "/usr/lib", "/usr/include", "/usr/lib/pkgconfig"),
            "libjpeg": ("3.2.0", "/usr", "/usr/lib", "/usr/include", "/usr/lib/pkgconfig"),
            "libxml-2.0": ("2.12.7", "/usr", "/usr/lib", "/usr/include/libxml2", "/usr/lib/pkgconfig"),
        }

        def command_output(command):
            command = tuple(str(value) for value in command)
            if command == ("/usr/bin/cc", "--version"):
                return "cc (Ubuntu 13.3.0) 13.3.0\n"
            if command == ("/usr/bin/cmake", "--version"):
                return "cmake version 4.4.4\n"
            if command == ("pkg-config", "--version"):
                return "1.8.1\n"
            if command[:2] == ("pkg-config", "--modversion"):
                return pkg_modules[command[2]][0] + "\n"
            if command[0] == "pkg-config" and command[1].startswith("--variable="):
                fields = {"prefix": 1, "libdir": 2, "includedir": 3, "pcfiledir": 4}
                return pkg_modules[command[2]][fields[command[1].split("=", 1)[1]]] + "\n"
            if command[0] == "dpkg-query":
                return (
                    "build-essential\t12.10ubuntu1\tamd64\n"
                    "libexif-dev\t0.6.24-1ubuntu0.22.04.1\tamd64\n"
                    "libxml2-dev\t2.9.13+dfsg-1ubuntu0.5\tamd64\n"
                )
            if command == ("ldd", str(self.binary)):
                return (
                    "libexif.so.12 => /lib/libexif.so.12 (0x0001)\n"
                    "libxml2.so.2 => /lib/libxml2.so.2 (0x0002)\n"
                    "/lib64/ld-linux-x86-64.so.2 (0x0003)\n"
                )
            raise AssertionError(f"unexpected command: {command}")

        output = self.root / "dependencies.json"
        environment = {
            "GITHUB_REPOSITORY": "ahmetomerv/photoc",
            "GITHUB_SHA": "c" * 40,
            "GITHUB_REF": "refs/tags/v0.7.1",
            "GITHUB_WORKFLOW_REF": "ahmetomerv/photoc/.github/workflows/release.yml@refs/tags/v0.7.1",
            "GITHUB_WORKFLOW_SHA": "d" * 40,
            "GITHUB_RUN_ID": "12345",
            "GITHUB_RUN_ATTEMPT": "2",
            "RUNNER_OS": "Linux",
            "RUNNER_ARCH": "X64",
            "ImageOS": "ubuntu22",
            "ImageVersion": "20261020.1",
        }
        with mock.patch.object(release, "_command_output", side_effect=command_output):
            release.write_dependency_manifest(
                "v0.7.1", "linux-x86_64", self.binary, self.build,
                self.workflow, output, environment
            )

        manifest = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(manifest["schemaVersion"], 1)
        self.assertEqual(manifest["source"]["commit"], "c" * 40)
        self.assertEqual(manifest["build"]["workflow"]["commit"], "d" * 40)
        self.assertEqual(
            manifest["build"]["workflow"]["actions"],
            [
                {"name": "actions/attest", "commit": "b" * 40, "version": "v4.2.2"},
                {"name": "actions/checkout", "commit": "a" * 40, "version": "v7.0.1"},
            ],
        )
        self.assertEqual(manifest["toolchain"]["compiler"]["id"], "GNU")
        self.assertEqual(manifest["toolchain"]["compiler"]["version"], "13.3.0")
        self.assertEqual(
            [entry["module"] for entry in manifest["dependencies"]["pkgConfig"]],
            ["libexif", "libturbojpeg", "libjpeg", "libxml-2.0"],
        )
        self.assertIn(
            {"name": "libexif-dev", "version": "0.6.24-1ubuntu0.22.04.1", "architecture": "amd64"},
            manifest["dependencies"]["packages"],
        )
        self.assertEqual(
            manifest["dependencies"]["linkedLibraries"][0],
            {"name": "libexif.so.12", "path": "/lib/libexif.so.12"},
        )
        self.assertEqual(json.loads(output.read_text(encoding="utf-8")), manifest)

    def test_manifest_refuses_mutable_action_references(self):
        self.workflow.write_text(
            "steps:\n  - uses: actions/checkout@v7\n", encoding="utf-8"
        )
        with self.assertRaisesRegex(ValueError, "immutable commit SHA"):
            release.workflow_actions(self.workflow)

    def test_macos_dependency_sources_record_homebrew_versions_and_linked_paths(self):
        def command_output(command):
            command = tuple(str(value) for value in command)
            if command[:3] == ("brew", "list", "--versions"):
                return (
                    "cmake 4.4.4\n"
                    "fish 4.1.2\n"
                    "jpeg-turbo 3.1.2\n"
                    "libexif 0.6.25\n"
                    "libxml2 2.15.1\n"
                    "pkgconf 2.5.1\n"
                )
            if command == ("otool", "-L", str(self.binary)):
                return (
                    f"{self.binary}:\n"
                    "\t/opt/homebrew/opt/libexif/lib/libexif.12.dylib "
                    "(compatibility version 16.0.0, current version 16.4.0)\n"
                    "\t/usr/lib/libSystem.B.dylib "
                    "(compatibility version 1.0.0, current version 1351.0.0)\n"
                )
            raise AssertionError(f"unexpected command: {command}")

        with mock.patch.object(release, "_command_output", side_effect=command_output):
            manager, packages = release._system_packages("darwin-arm64")
            libraries = release._linked_libraries("darwin-arm64", self.binary)

        self.assertEqual(manager, "homebrew")
        self.assertIn({"name": "libexif", "version": "0.6.25"}, packages)
        self.assertEqual(
            libraries,
            [
                {
                    "name": "libexif.12.dylib",
                    "path": "/opt/homebrew/opt/libexif/lib/libexif.12.dylib",
                },
                {"name": "libSystem.B.dylib", "path": "/usr/lib/libSystem.B.dylib"},
            ],
        )


if __name__ == "__main__":
    unittest.main()

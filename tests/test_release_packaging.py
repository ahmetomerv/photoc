#!/usr/bin/env python3
"""Exercise release archives and checksum gates with the actual built executable."""

import contextlib
import importlib.util
import io
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
                         "README.md", "assets/photoc.svg", "assets/photoc-light.svg",
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


if __name__ == "__main__":
    unittest.main()

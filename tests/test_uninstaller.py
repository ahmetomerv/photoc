#!/usr/bin/env python3
"""Test tracked uninstall and preservation of unrelated files, offline."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

from test_installer import InstallerFixture


UNINSTALLER = Path(__file__).resolve().parents[1] / "scripts/uninstall.sh"


class UninstallerTests(InstallerFixture):
    def install(self, *arguments):
        result = self.run_installer(*arguments)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.executed.unlink()

    def run_uninstaller(self, *arguments):
        result = subprocess.run(["/bin/sh", str(UNINSTALLER), *arguments],
                                env=self.environment, capture_output=True, text=True,
                                timeout=15)
        self.assertFalse(self.executed.exists(), "uninstaller executed the binary")
        return result

    def assert_installation_intact(self):
        self.assertEqual(self.destination.read_bytes(), self.binary.read_bytes())
        self.assertTrue(self.receipt.exists())
        self.assertEqual(list(self.destination.parent.glob(".photoc-uninstall.*")), [])

    def test_preview_and_apply_preserve_unrelated_files(self):
        self.install()
        preserved = [self.home / ".zshrc", self.home / ".config/photoc/settings",
                     self.destination.parent / "another-tool",
                     self.home / ".local/share/man/man1/photoc.1"]
        for path in preserved:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("user data")
        snapshots = {path: path.read_bytes() for path in preserved}
        receipt_before = self.receipt.read_bytes()
        for arguments in ((), ("--dry-run",)):
            result = self.run_uninstaller(*arguments)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("Would remove", result.stdout)
            self.assertIn("Dry run", result.stdout)
            self.assert_installation_intact()
            self.assertEqual(self.receipt.read_bytes(), receipt_before)
        result = self.run_uninstaller("--apply")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.destination.exists())
        self.assertFalse(self.receipt.exists())
        self.assertTrue(self.destination.parent.is_dir())
        self.assertEqual(list(self.destination.parent.glob(".photoc-uninstall.*")), [])
        for path, contents in snapshots.items():
            self.assertEqual(path.read_bytes(), contents)

    def test_custom_install_directory(self):
        directory = self.root / "bin 'with spaces'"
        self.install("--install-dir", str(directory))
        result = self.run_uninstaller("--install-dir", str(directory), "--apply")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(directory.is_dir())
        self.assertEqual(list(directory.iterdir()), [])

    def test_linux_uninstall(self):
        self.environment.update(MOCK_OS="Linux", MOCK_ARCH="x86_64")
        self.install()
        result = self.run_uninstaller("--apply")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.destination.exists())

    def test_other_hard_links_are_preserved(self):
        self.install()
        backup = self.root / "user-backup"
        os.link(self.destination, backup)
        result = self.run_uninstaller("--apply")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(backup.read_bytes(), self.binary.read_bytes())

    def test_missing_installation_is_idempotent(self):
        result = self.run_uninstaller("--apply")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("No tracked", result.stdout)
        self.assertFalse(self.destination.parent.exists())
        self.install()
        self.assertEqual(self.run_uninstaller("--apply").returncode, 0)
        self.assertEqual(self.run_uninstaller("--apply").returncode, 0)

    def test_untracked_binary_is_preserved(self):
        self.destination.parent.mkdir(parents=True)
        self.destination.write_text("user-owned photoc")
        result = self.run_uninstaller("--apply")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ownership receipt", result.stderr)
        self.assertEqual(self.destination.read_text(), "user-owned photoc")

    def test_modified_binary_is_preserved(self):
        self.install()
        self.destination.write_text("modified binary")
        receipt_before = self.receipt.read_bytes()
        result = self.run_uninstaller("--apply")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("changed or is unsafe", result.stderr)
        self.assertEqual(self.destination.read_text(), "modified binary")
        self.assertEqual(self.receipt.read_bytes(), receipt_before)

    def test_identical_replacement_is_preserved(self):
        self.install()
        replacement = self.root / "replacement"
        replacement.write_bytes(self.destination.read_bytes())
        os.replace(replacement, self.destination)
        result = self.run_uninstaller("--apply")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("changed or is unsafe", result.stderr)
        self.assert_installation_intact()

    def test_binary_symlink_or_directory_is_preserved(self):
        for kind in ("symlink", "directory"):
            with self.subTest(kind=kind):
                self.install()
                self.destination.unlink()
                if kind == "symlink":
                    self.destination.symlink_to(self.binary)
                else:
                    self.destination.mkdir()
                    (self.destination / "user-file").write_text("keep")
                result = self.run_uninstaller("--apply")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("unsafe", result.stderr)
                self.assertTrue(self.receipt.exists())
                if kind == "symlink":
                    self.assertTrue(self.destination.is_symlink())
                    self.destination.unlink()
                else:
                    self.assertEqual((self.destination / "user-file").read_text(), "keep")
                    shutil.rmtree(self.destination)
                self.receipt.unlink()

    def test_invalid_receipt_is_not_executed(self):
        self.install()
        before = self.destination.read_bytes()
        self.receipt.write_text('photoc-install-v1\n$(touch "$MOCK_EXECUTED")\nidentity 0:0\n')
        result = self.run_uninstaller("--apply")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid ownership receipt", result.stderr)
        self.assertEqual(self.destination.read_bytes(), before)
        self.assertTrue(self.receipt.exists())

    def test_symlink_receipt_is_preserved(self):
        self.install()
        saved_receipt = self.root / "saved-receipt"
        self.receipt.rename(saved_receipt)
        self.receipt.symlink_to(saved_receipt)
        result = self.run_uninstaller("--apply")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("regular ownership receipt", result.stderr)
        self.assertTrue(self.receipt.is_symlink())
        self.assertEqual(self.destination.read_bytes(), self.binary.read_bytes())

    def test_stale_receipt_cleanup(self):
        self.install()
        self.destination.unlink()
        preview = self.run_uninstaller()
        self.assertEqual(preview.returncode, 0, preview.stderr)
        self.assertTrue(self.receipt.exists())
        result = self.run_uninstaller("--apply")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.receipt.exists())

    def test_shasum_fallback(self):
        self.install()
        (self.tools / "sha256sum").unlink()
        result = self.run_uninstaller("--apply")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.destination.exists())

    def test_hash_failure_preserves_installation(self):
        self.install()
        self.environment["MOCK_HASH_FAIL"] = "1"
        result = self.run_uninstaller("--apply")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("checksum", result.stderr)
        self.assert_installation_intact()

    def test_racing_replacement_is_preserved_in_quarantine(self):
        self.install()
        real_mv = shutil.which("mv")
        (self.tools / "mv").unlink()
        self.write_tool("mv", f"#!{sys.executable}\n" + f'''
import os
from pathlib import Path
import sys
source = Path(sys.argv[1])
if source.name == "photoc":
    source.write_text("replacement during uninstall")
os.execv({real_mv!r}, [{real_mv!r}, *sys.argv[1:]])
''')
        result = self.run_uninstaller("--apply")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("staged files were preserved", result.stderr)
        quarantine = list(self.destination.parent.glob(".photoc-uninstall.*"))
        self.assertEqual(len(quarantine), 1)
        self.assertEqual((quarantine[0] / "photoc").read_text(), "replacement during uninstall")
        self.assertTrue((quarantine[0] / ".photoc-install-receipt").exists())

    def test_staging_failure_preserves_installation(self):
        self.install()
        (self.tools / "mktemp").unlink()
        self.write_tool("mktemp", "#!/bin/sh\nexit 1\n")
        result = self.run_uninstaller("--apply")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("cannot stage removal", result.stderr)
        self.assert_installation_intact()

    def test_help_and_invalid_options(self):
        result = self.run_uninstaller("--help")
        self.assertEqual(result.returncode, 0)
        self.assertIn("Usage:", result.stdout)
        self.assertEqual(result.stderr, "")
        for args in (("--force",), ("--install-dir",)):
            result = self.run_uninstaller(*args)
            self.assertEqual(result.returncode, 2)
            self.assertIn("uninstall: error:", result.stderr)
        result = self.run_uninstaller("--install-dir", "relative", "--apply")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must be absolute", result.stderr)


if __name__ == "__main__":
    unittest.main()

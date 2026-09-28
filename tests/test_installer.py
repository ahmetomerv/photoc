#!/usr/bin/env python3
"""Offline installer tests: mock GitHub downloads, never touch the user's bin."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import sys
import tempfile
import unittest


INSTALLER = Path(__file__).resolve().parents[1] / "scripts/install.sh"
REPOSITORY = "https://github.com/ahmetomerv/photoc"


class InstallerFixture(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="photoc-installer-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.tools = self.root / "tools"
        self.tools.mkdir()
        self.downloads = self.root / "temporary downloads"
        self.downloads.mkdir()
        self.home = self.root / "home"
        self.home.mkdir()
        self.destination = self.home / ".local/bin/photoc"
        self.receipt = self.destination.parent / ".photoc-install-receipt"
        self.calls = self.root / "curl-calls.jsonl"
        self.binary = self.root / "release-binary"
        self.manifest = self.root / "SHA256SUMS"
        self.executed = self.root / "executed"
        self.environment = {
            **os.environ,
            "PATH": str(self.tools),
            "HOME": str(self.home),
            "TMPDIR": str(self.downloads),
            "MOCK_OS": "Darwin",
            "MOCK_ARCH": "arm64",
            "MOCK_TAG": "v1.2.3",
            "MOCK_BINARY": str(self.binary),
            "MOCK_MANIFEST": str(self.manifest),
            "MOCK_CALLS": str(self.calls),
            "MOCK_EXECUTED": str(self.executed),
            "MOCK_DESTINATION": str(self.destination),
        }
        for tool in ("cat", "chmod", "cp", "ln", "mkdir", "mktemp", "rm", "rmdir",
                     "mv", "awk", "sed"):
            location = shutil.which(tool)
            if location is None:
                self.fail(f"required test tool is missing: {tool}")
            (self.tools / tool).symlink_to(location)
        self.write_tool("uname", """#!/bin/sh
case "$1" in
    -s) printf '%s\\n' "$MOCK_OS" ;;
    -m) printf '%s\\n' "$MOCK_ARCH" ;;
    *) exit 1 ;;
esac
""")
        self.write_tool("stat", f"#!{sys.executable}\n" + '''
from pathlib import Path
import sys
assert sys.argv[1] in ("-f", "-c")
assert sys.argv[2] == "%d:%i"
info = Path(sys.argv[3]).stat()
print(f"{info.st_dev}:{info.st_ino}")
''')
        self.write_tool("curl", f"#!{sys.executable}\n" + '''
import json
import os
from pathlib import Path
import signal
import sys

args = sys.argv[1:]
assert args[0] == "--disable", "curlrc must be disabled"
for flag in ("--proto", "--proto-redir"):
    assert args[args.index(flag) + 1] == "=https", "HTTPS must be enforced"
url = args[-1]
with open(os.environ["MOCK_CALLS"], "a") as stream:
    stream.write(json.dumps(url) + "\\n")
if os.environ.get("MOCK_SIGNAL"):
    os.kill(os.getppid(), signal.SIGTERM)
    sys.exit(22)
if os.environ.get("MOCK_DOWNLOAD_FAIL") in ("all", url.rsplit("/", 1)[-1]):
    sys.exit(22)
repository = "https://github.com/ahmetomerv/photoc"
tag = os.environ["MOCK_TAG"]
if url == repository + "/releases/latest":
    sys.stdout.write(os.environ.get("MOCK_LATEST_URL", repository + "/releases/tag/" + tag))
else:
    assert url.startswith(repository + "/releases/download/" + tag + "/"), url
    name = url.rsplit("/", 1)[-1]
    expected = {("Darwin", "arm64"): "darwin-arm64",
                ("Darwin", "aarch64"): "darwin-arm64",
                ("Darwin", "x86_64"): "darwin-x86_64",
                ("Linux", "x86_64"): "linux-x86_64",
                ("Linux", "amd64"): "linux-x86_64"}[
                    os.environ["MOCK_OS"], os.environ["MOCK_ARCH"]]
    assert name in ("SHA256SUMS", "photoc-" + expected), name
    source = "MOCK_MANIFEST" if name == "SHA256SUMS" else "MOCK_BINARY"
    Path(args[args.index("--output") + 1]).write_bytes(Path(os.environ[source]).read_bytes())
''')
        hashing = f"#!{sys.executable}\n" + '''
import hashlib
import os
from pathlib import Path
import sys
if os.environ.get("MOCK_HASH_FAIL"):
    sys.exit(1)
path = sys.argv[-1]
print(hashlib.sha256(Path(path).read_bytes()).hexdigest() + "  " + path)
'''
        for name in ("sha256sum", "shasum"):
            location = shutil.which(name)
            if location is not None:
                # Exercise the real platform's digest tools where available.
                self.write_tool(name, '#!/bin/sh\n'
                                '[ -z "${MOCK_HASH_FAIL:-}" ] || exit 1\n'
                                f'exec {shlex.quote(location)} "$@"\n')
            else:
                self.write_tool(name, hashing)
        self.set_binary()

    def write_tool(self, name, contents):
        path = self.tools / name
        path.write_text(contents)
        path.chmod(0o755)

    def set_binary(self, output="photoc 1.2.3", status=0):
        self.binary.write_text(f"""#!/bin/sh
printf executed > "$MOCK_EXECUTED"
case "${{MOCK_RACE:-}}" in
    file) printf original > "$MOCK_DESTINATION" ;;
    directory) mkdir "$MOCK_DESTINATION" ;;
    symlink) ln -s "$MOCK_BINARY" "$MOCK_DESTINATION" ;;
    receipt) printf 'user receipt' > "${{MOCK_DESTINATION%/*}}/.photoc-install-receipt" ;;
esac
printf '%s\\n' '{output}'
exit {status}
""")
        self.digest = hashlib.sha256(self.binary.read_bytes()).hexdigest()
        self.manifest.write_text("".join(
            f"{self.digest}  photoc-{platform}\n"
            for platform in ("darwin-arm64", "darwin-x86_64", "linux-x86_64")))

    def run_installer(self, *arguments):
        result = subprocess.run(["/bin/sh", str(INSTALLER), *arguments],
                                env=self.environment, capture_output=True, text=True,
                                timeout=15)
        self.assertEqual(list(self.downloads.iterdir()), [], "download directory leaked")
        if self.destination.parent.exists():
            self.assertEqual(list(self.destination.parent.glob(".photoc-install.*")), [],
                             "installation staging directory leaked")
        return result

    def assert_failed(self, result, message):
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(message, result.stderr)
        self.assertFalse(self.destination.exists())
        self.assertFalse(self.destination.is_symlink())
        self.assertFalse(self.receipt.exists())


class InstallerTests(InstallerFixture):

    def test_platforms_and_pinned_latest(self):
        for os_name, arch in (("Darwin", "arm64"), ("Darwin", "x86_64"),
                              ("Linux", "x86_64"), ("Linux", "amd64")):
            with self.subTest(os=os_name, arch=arch):
                self.environment.update(MOCK_OS=os_name, MOCK_ARCH=arch)
                result = self.run_installer()
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(self.destination.read_bytes(), self.binary.read_bytes())
                self.assertEqual(self.destination.stat().st_mode & 0o777, 0o755)
                self.assertEqual(self.receipt.stat().st_mode & 0o777, 0o600)
                info = self.destination.stat()
                self.assertEqual(self.receipt.read_text().splitlines(), [
                    "photoc-install-v1", "sha256 " + self.digest,
                    f"identity {info.st_dev}:{info.st_ino}"])
                self.assertIn("Installed photoc 1.2.3", result.stdout)
                calls = [json.loads(line) for line in self.calls.read_text().splitlines()]
                self.assertEqual(calls[0], REPOSITORY + "/releases/latest")
                self.assertTrue(all("/releases/download/v1.2.3/" in url for url in calls[1:]))
                self.destination.unlink()
                self.receipt.unlink()
                self.calls.unlink()

    def test_specific_tag_and_shasum_fallback(self):
        (self.tools / "sha256sum").unlink()
        result = self.run_installer("--version", "v1.2.3")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("/releases/latest", self.calls.read_text())

    def test_custom_path_with_spaces_and_quotes(self):
        directory = self.root / "bin 'with spaces'"
        result = self.run_installer("--install-dir", str(directory))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((directory / "photoc").exists())
        export = next(line.strip() for line in result.stdout.splitlines()
                      if line.strip().startswith("export PATH="))
        path_result = subprocess.run(["/bin/sh", "-c", export + '; printf "%s" "$PATH"'],
                                     env=self.environment, capture_output=True, text=True)
        self.assertEqual(path_result.returncode, 0, path_result.stderr)
        self.assertEqual(path_result.stdout, str(directory) + ":" + str(self.tools))

    def test_path_already_configured(self):
        self.environment["PATH"] += ":" + str(self.destination.parent)
        result = self.run_installer()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("export PATH=", result.stdout)

    def test_unsupported_platform(self):
        for os_name, arch in (("Linux", "aarch64"), ("FreeBSD", "x86_64")):
            with self.subTest(os=os_name, arch=arch):
                self.environment.update(MOCK_OS=os_name, MOCK_ARCH=arch)
                self.assert_failed(self.run_installer(), "unsupported platform")
                self.assertFalse(self.calls.exists())

    def test_invalid_arguments(self):
        for arguments in (("--bad",), ("--version",), ("--install-dir",)):
            with self.subTest(arguments=arguments):
                result = self.run_installer(*arguments)
                self.assertEqual(result.returncode, 2)
                self.assertIn("install: error:", result.stderr)
        for arguments in (("--version", "../../bad"), ("--version", "bad?tag"),
                          ("--install-dir", "relative"), ("--install-dir", "/bin:other")):
            with self.subTest(arguments=arguments):
                self.assert_failed(self.run_installer(*arguments), "install: error:")
        self.assertFalse(self.calls.exists())

    def test_help(self):
        result = self.run_installer("--help")
        self.assertEqual(result.returncode, 0)
        self.assertIn("Usage:", result.stdout)
        self.assertEqual(result.stderr, "")
        self.assertFalse(self.calls.exists())

    def test_bad_latest_redirect(self):
        self.environment["MOCK_LATEST_URL"] = "https://example.com/releases/tag/v1.2.3"
        self.assert_failed(self.run_installer(), "did not redirect to a release tag")

    def test_download_failures(self):
        for name in ("latest", "SHA256SUMS", "photoc-darwin-arm64"):
            with self.subTest(asset=name):
                self.environment["MOCK_DOWNLOAD_FAIL"] = name
                self.assert_failed(self.run_installer(), "cannot")
                self.assertFalse(self.executed.exists())

    def test_checksum_failures_never_execute_binary(self):
        asset = "photoc-darwin-arm64"
        for manifest in (f"{'0' * 64}  {asset}\n", "", f"broken  {asset}\n",
                         f"{self.digest}  {asset}\n{self.digest}  {asset}\n"):
            with self.subTest(manifest=manifest):
                self.manifest.write_text(manifest)
                self.assert_failed(self.run_installer(), "checksum")
                self.assertFalse(self.executed.exists())
                self.assertFalse(self.destination.parent.exists())

    def test_hash_tool_failure(self):
        self.environment["MOCK_HASH_FAIL"] = "1"
        self.assert_failed(self.run_installer(), "cannot calculate SHA-256")
        self.assertFalse(self.executed.exists())

    def test_missing_hash_tool(self):
        (self.tools / "sha256sum").unlink()
        (self.tools / "shasum").unlink()
        self.assert_failed(self.run_installer(), "requires sha256sum or shasum")
        self.assertFalse(self.calls.exists())

    def test_crlf_uppercase_binary_mode_checksum(self):
        self.manifest.write_bytes(f"{self.digest.upper()} *photoc-darwin-arm64\r\n".encode())
        result = self.run_installer()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_unrunnable_or_wrong_binary(self):
        for output, status in (("photoc 1.2.3", 127), ("another-tool 1.2.3", 0)):
            with self.subTest(output=output, status=status):
                self.set_binary(output=output, status=status)
                self.assert_failed(self.run_installer(), "downloaded")

    def test_existing_entries_are_preserved(self):
        self.destination.parent.mkdir(parents=True)
        for kind in ("file", "directory", "symlink"):
            with self.subTest(kind=kind):
                if kind == "file":
                    self.destination.write_text("original")
                elif kind == "directory":
                    self.destination.mkdir()
                else:
                    self.destination.symlink_to(self.root / "missing-target")
                result = self.run_installer()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("already exists", result.stderr)
                self.assertFalse(self.calls.exists())
                if kind == "directory":
                    self.assertEqual(list(self.destination.iterdir()), [])
                    self.destination.rmdir()
                else:
                    if kind == "file":
                        self.assertEqual(self.destination.read_text(), "original")
                    else:
                        self.assertTrue(self.destination.is_symlink())
                    self.destination.unlink()

    def test_entries_created_during_install_are_preserved(self):
        for kind in ("file", "directory", "symlink"):
            with self.subTest(kind=kind):
                self.environment["MOCK_RACE"] = kind
                result = self.run_installer()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("cannot install", result.stderr)
                if kind == "directory":
                    self.assertEqual(list(self.destination.iterdir()), [])
                    self.destination.rmdir()
                else:
                    if kind == "file":
                        self.assertEqual(self.destination.read_text(), "original")
                    else:
                        self.assertTrue(self.destination.is_symlink())
                    self.destination.unlink()

    def test_signal_cleans_downloads(self):
        self.environment["MOCK_SIGNAL"] = "1"
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.destination.exists())

    def test_existing_receipt_is_preserved(self):
        self.receipt.parent.mkdir(parents=True)
        self.receipt.write_text("user receipt")
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("already exists", result.stderr)
        self.assertEqual(self.receipt.read_text(), "user receipt")
        self.assertFalse(self.calls.exists())

    def test_receipt_created_during_install_is_preserved(self):
        self.environment["MOCK_RACE"] = "receipt"
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("cannot record", result.stderr)
        self.assertEqual(self.receipt.read_text(), "user receipt")
        self.assertFalse(self.destination.exists())


if __name__ == "__main__":
    unittest.main()

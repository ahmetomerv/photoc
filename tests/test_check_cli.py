#!/usr/bin/env python3
"""Read-only JPEG audit through the CLI, with generated malformed inputs."""

import json
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BINARY = os.environ["PHOTOC_CHECK_TEST_BINARY"]
BASE = (ROOT / "tests/fixtures/jpeg/no_exif.jpg").read_bytes()
EXIF = (ROOT / "tests/fixtures/jpeg_metadata/exif_only.jpg").read_bytes()


def marker(kind, payload):
    return bytes((0xFF, kind)) + struct.pack(">H", len(payload) + 2) + payload


class CheckTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="photoc-check-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()

    def write(self, name, data=BASE):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def invoke(self, *args, expected=0):
        result = subprocess.run([BINARY, *map(str, args)], cwd=self.root,
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def audit(self, path, *options, expected=0):
        result = self.invoke("check", path, "--json", *options, expected=expected)
        payload = json.loads(result.stdout)
        self.assertEqual(set(payload), {"summary", "files"})
        self.assertEqual(set(payload["summary"]),
                         {"files_checked", "ok", "warnings", "errors", "files_skipped"})
        for row in payload["files"]:
            self.assertEqual(set(row), {"path", "status", "code", "message"})
            self.assertIn(row["status"], ("ok", "warning", "error"))
        return payload, result

    def mixed(self):
        folder = self.root / "photos"
        self.write("photos/a-ok.JPG")
        self.write("photos/b-warning.jpeg", BASE[:2] + marker(0xE1, b"Exif\0\0broken") + BASE[2:])
        self.write("photos/c-garbage.jpg", b"garbage")
        self.write("photos/d-empty.jpg", b"")
        self.write("photos/ignored.png", b"not a JPEG")
        self.write("photos/nested/e-ok.jpeg", EXIF)
        return folder

    def test_valid_jpegs_and_exif_are_readable_and_untouched(self):
        gray = (ROOT / "tests/fixtures/jpeg_metadata/gray_icc.jpg").read_bytes()
        for name, content in (("valid.jpg", BASE), ("exif.JPG", EXIF), ("gray.jpg", gray)):
            source = self.write(name, content)
            before = source.stat()
            payload, result = self.audit(source)
            self.assertEqual(payload["summary"], {"files_checked": 1, "ok": 1,
                             "warnings": 0, "errors": 0, "files_skipped": 0})
            self.assertEqual(payload["files"][0]["code"], "readable")
            self.assertEqual(result.stderr, "")
            self.assertEqual(source.read_bytes(), content)
            after = source.stat()
            self.assertEqual((after.st_mode, after.st_mtime_ns, after.st_size),
                             (before.st_mode, before.st_mtime_ns, before.st_size))

    def test_empty_garbage_and_truncated_header_are_errors(self):
        for name, data, code in (("empty.jpg", b"", "empty_file"),
                                 ("garbage.jpg", b"not a jpeg", "invalid_signature"),
                                 ("truncated.jpg", BASE[:20], "truncated_jpeg")):
            source = self.write(name, data)
            payload, result = self.audit(source, "--quiet", expected=1)
            self.assertEqual(payload["files"][0]["status"], "error")
            self.assertEqual(payload["files"][0]["code"], code)
            self.assertEqual(payload["summary"]["errors"], 1)
            self.assertIn("photoc check:", result.stderr)
            self.assertEqual(source.read_bytes(), data)

    def test_decoder_recovery_remains_warning(self):
        source = self.write("missing-eoi.jpg", BASE[:-2])
        payload, result = self.audit(source)
        self.assertEqual(payload["files"][0]["status"], "warning")
        self.assertEqual(payload["files"][0]["code"], "truncated_jpeg")
        self.assertIn("recovered", payload["files"][0]["message"])
        self.assertEqual(result.stderr, "")
        data = bytearray(BASE)
        data[data.index(b"JFIF\0") + 5] = 9
        payload, _ = self.audit(self.write("jfif.jpg", data))
        self.assertEqual(payload["files"][0]["status"], "warning")
        self.assertEqual(payload["files"][0]["code"], "jpeg_warning")

    def test_readable_pixels_with_malformed_exif_warn(self):
        cases = [b"Exif", b"Exif\0\0broken", b"Exif\0\0II*\0\xff\xff\xff\xff",
                 b"Exif\0\0II*\0\x08\0\0\0\x01\0"]
        offset = EXIF.index(b"Exif\0\0")
        length = int.from_bytes(EXIF[offset - 2:offset], "big") - 2
        segment = bytearray(EXIF[offset:offset + length])
        tiff = memoryview(segment)[6:]
        directory = struct.unpack_from("<I", tiff, 4)[0]
        entries = struct.unpack_from("<H", tiff, directory)[0]
        for i in range(entries):
            entry = directory + 2 + 12 * i
            if struct.unpack_from("<H", tiff, entry)[0] == 0x010F:
                struct.pack_into("<I", tiff, entry + 8, 0xFFFFFFF0)
                break
        cases.append(segment)
        # Big-endian EXIF with an out-of-range value, a directory cycle, and
        # overlapping value references whose aggregate copies exceed the budget.
        be = b"MM\0*" + struct.pack(">I", 8) + struct.pack(">H", 1)
        be += struct.pack(">HHII", 0x010F, 2, 5, 0xFFFFFFF0) + b"\0" * 4
        cases.append(b"Exif\0\0" + be)
        cyclic = b"II*\0" + struct.pack("<IHI", 8, 0, 8)
        cases.append(b"Exif\0\0" + cyclic)
        repeated = b"II*\0" + struct.pack("<IH", 8, 100)
        repeated += struct.pack("<HHII", 0x010F, 2, 63000, 1214) * 100
        repeated += b"\0" * 4 + b"A" * 63000
        cases.append(b"Exif\0\0" + repeated)
        for i, payload in enumerate(cases):
            source = self.write(f"exif-{i}.jpg", BASE[:2] + marker(0xE1, payload) + BASE[2:])
            result, _ = self.audit(source)
            self.assertEqual(result["files"][0]["status"], "warning")
            self.assertEqual(result["files"][0]["code"], "malformed_exif")
            self.invoke("focus", source, "--quiet")

    def test_mixed_directory_recursion_symlinks_and_determinism(self):
        folder = self.mixed()
        (folder / "linked.jpg").symlink_to(folder / "a-ok.JPG")
        (folder / "linked-directory").symlink_to(folder / "nested", target_is_directory=True)
        (folder / "loop").symlink_to(folder, target_is_directory=True)
        before = {p.relative_to(folder): p.read_bytes() for p in folder.rglob("*") if p.is_file() and not p.is_symlink()}
        payload, first = self.audit(folder, expected=1)
        self.assertEqual(payload["summary"], {"files_checked": 4, "ok": 1,
                         "warnings": 1, "errors": 2, "files_skipped": 1})
        recursive, _ = self.audit(folder, "--recursive", expected=1)
        self.assertEqual(recursive["summary"]["files_checked"], 5)
        self.assertEqual(recursive["summary"]["ok"], 2)
        self.assertEqual([row["path"] for row in recursive["files"]],
                         ["a-ok.JPG", "b-warning.jpeg", "c-garbage.jpg", "d-empty.jpg", "nested/e-ok.jpeg"])
        self.assertEqual(self.invoke("check", folder, "--json", expected=1).stdout, first.stdout)
        self.assertEqual(self.invoke("check", folder, "--json", expected=1).stderr, first.stderr)
        self.assertEqual(before, {p: (folder / p).read_bytes() for p in before})

    def test_only_errors_excludes_warnings_and_keeps_summary(self):
        folder = self.mixed()
        payload, _ = self.audit(folder, "--only-errors", expected=1)
        self.assertEqual([row["status"] for row in payload["files"]], ["error", "error"])
        self.assertEqual(payload["summary"]["files_checked"], 4)
        self.assertEqual(payload["summary"]["warnings"], 1)
        human = self.invoke("check", folder, "--only-errors", expected=1).stdout
        self.assertIn("ERROR    c-garbage.jpg", human)
        self.assertNotIn("WARNING  b-warning", human)
        self.assertNotIn("OK       a-ok", human)
        self.assertIn("Warnings: 1", human)
        payload, _ = self.audit(self.write("okay.jpg"), "--only-errors")
        self.assertEqual(payload["files"], [])
        self.assertEqual(payload["summary"]["ok"], 1)

    def test_verbosity_keeps_results_and_json_schema(self):
        folder = self.mixed()
        normal, _ = self.audit(folder, expected=1)
        for flag in ("--quiet", "--verbose", "-q", "-v"):
            payload, result = self.audit(folder, flag, expected=1)
            self.assertEqual(payload, normal)
            self.assertIn("photoc check:", result.stderr)
            if flag in ("--verbose", "-v"):
                self.assertIn("mode: full decode audit", result.stderr)
                self.assertIn("JPEG files discovered: 4", result.stderr)
                self.assertIn("workers: 1", result.stderr)
            else:
                self.assertNotIn("verbose:", result.stderr)
        normal = self.invoke("check", folder, expected=1)
        quiet = self.invoke("check", folder, "--quiet", expected=1)
        self.assertIn("Checks cover", normal.stdout)
        self.assertNotIn("Checks cover", quiet.stdout)
        self.assertIn("WARNING  b-warning.jpeg", quiet.stdout)
        self.assertIn("Errors: 2", quiet.stdout)
        self.assertIn("c-garbage.jpg", quiet.stderr)

    def test_empty_directory_and_option_placement(self):
        self.write("notes.txt", b"not a photo")
        payload, _ = self.audit(self.root)
        self.assertEqual(payload["summary"]["files_checked"], 0)
        self.assertEqual(payload["files"], [])
        source = self.write("-image.jpg")
        result = self.invoke("--json", "--only-errors", "check", "--", source.name)
        self.assertEqual(json.loads(result.stdout)["summary"]["ok"], 1)
        folder = self.mixed()
        result = self.invoke("--recursive", "--only-errors", "--json", "check", folder, expected=1)
        self.assertEqual(json.loads(result.stdout)["summary"]["files_checked"], 5)

    def test_usage_and_operational_errors(self):
        valid = self.write("valid.jpg")
        for args in (("check",), ("check", valid, valid), ("check", valid, "--recursive"),
                     ("check", valid, "--bogus"), ("check", valid, "--apply"),
                     ("check", valid, "--threshold", "20"), ("check", valid, "--quiet", "--verbose"),
                     ("exif", valid, "--only-errors")):
            result = self.invoke(*args, expected=2)
            self.assertEqual(result.stdout, "")
            self.assertTrue(result.stderr)
        for path in (self.root / "missing.jpg", self.write("photo.png")):
            self.assertEqual(self.invoke("check", path, "--json", expected=1).stdout, "")
        link = self.root / "link.jpg"
        link.symlink_to(valid)
        self.assertEqual(self.invoke("check", link, expected=1).stdout, "")

    def test_limits_are_incomplete_audits_not_corruption_claims(self):
        source = self.write("huge.jpg")
        with source.open("r+b") as file:
            file.truncate(512 * 1024 * 1024 + 1)
        payload, _ = self.audit(source, expected=1)
        self.assertEqual(payload["files"][0]["code"], "resource_limit")
        self.assertIn("not fully checked", payload["files"][0]["message"])
        data = bytearray(BASE)
        sof = data.index(b"\xff\xc0")
        struct.pack_into(">HH", data, sof + 5, 20000, 20000)
        payload, _ = self.audit(self.write("huge-pixels.jpg", data), expected=1)
        self.assertEqual(payload["files"][0]["code"], "resource_limit")
        sos = BASE.index(b"\xff\xda")
        scans = BASE[sos:-2] * 257
        payload, _ = self.audit(self.write("many-scans.jpg", BASE[:sos] + scans + b"\xff\xd9"), expected=1)
        self.assertEqual(payload["files"][0]["code"], "resource_limit")

    def test_control_character_paths_are_safe_and_json_escaped(self):
        source = self.write('line\n"name\\.jpg')
        payload, _ = self.audit(source)
        self.assertEqual(payload["files"][0]["path"], str(source))
        human = self.invoke("check", source).stdout
        self.assertIn('line\\x0a"name\\\\.jpg', human)
        self.assertNotIn('line\n"name', human)

    def test_malformed_marker_mutations_do_not_crash(self):
        generator = random.Random(1701)
        source = self.root / "fuzz.jpg"
        for i in range(48):
            data = bytearray(BASE)
            for _ in range(1 + i % 5):
                data[generator.randrange(len(data))] = generator.randrange(256)
            source.write_bytes(data)
            result = subprocess.run([BINARY, "check", str(source), "--json"],
                                    capture_output=True, text=True, timeout=15)
            self.assertIn(result.returncode, (0, 1), result.stderr)
            payload = json.loads(result.stdout)
            self.assertEqual(result.returncode, 1 if payload["files"][0]["status"] == "error" else 0)


if __name__ == "__main__":
    unittest.main()

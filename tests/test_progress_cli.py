#!/usr/bin/env python3
"""A real stderr PTY must not contaminate JSON stdout."""

import json
import os
from pathlib import Path
import pty
import selectors
import signal
import subprocess
import tempfile
import unittest

BINARY = os.environ["PHOTOC_PROGRESS_TEST_BINARY"]
FIXTURE = Path(__file__).resolve().parent / "fixtures/jpeg/with_exif.jpg"
# The runs that must show the spinner need the scan to outlast the 200 ms
# display delay on a fast host. Hard-linking one large synthetic stream (its
# markers are parsed, never decoded) gives that work without the cost of
# creating many distinct files; the disable and non-TTY checks use a small real
# directory instead so the large one is scanned as few times as possible.
SCAN_BYTES = 32 * 1024 * 1024
PROGRESS_FILES = 1500
PROBE_FILES = 200


def synthetic_jpeg(size):
    header = bytes.fromhex("ffd8"
                           "ffc0000b080002000301011100"
                           "ffda0008010100003f00")
    pattern = bytes(range(255))
    entropy = (pattern * (size // len(pattern) + 1))[:size]
    return header + entropy + b"\xff\xd9"


def invoke_tty(*args, interrupt_signal=None):
    master, slave = pty.openpty()
    process = subprocess.Popen([BINARY, *map(str, args)], stdout=subprocess.PIPE,
                               stderr=slave)
    os.close(slave)
    stdout_fd = process.stdout.fileno()
    selector = selectors.DefaultSelector()
    selector.register(master, selectors.EVENT_READ)
    selector.register(process.stdout, selectors.EVENT_READ)
    chunks = {master: [], stdout_fd: []}
    sent_interrupt = False
    while selector.get_map():
        for key, _ in selector.select(timeout=15):
            try:
                data = os.read(key.fd, 65536)
            except OSError:  # macOS/Linux PTY EOF may raise EIO.
                data = b""
            if data:
                chunks[key.fd].append(data)
                if (interrupt_signal is not None and key.fd == master and
                        not sent_interrupt and b"\x1b[K" in data):
                    process.send_signal(interrupt_signal)
                    sent_interrupt = True
            else:
                selector.unregister(key.fileobj)
    selector.close()
    process.wait(timeout=15)
    process.stdout.close()
    os.close(master)
    if interrupt_signal is not None and not sent_interrupt:
        raise AssertionError("operation finished before progress appeared")
    return (process.returncode,
            b"".join(chunks[stdout_fd]),
            b"".join(chunks[master]))


class ProgressCliTests(unittest.TestCase):
    def test_json_and_disable_rules(self):
        with tempfile.TemporaryDirectory(prefix="photoc-progress-") as directory:
            root = Path(directory)
            progress_root = root / "progress"
            probe_root = root / "probe"
            progress_root.mkdir()
            probe_root.mkdir()
            source = progress_root / "photo-00000.jpg"
            source.write_bytes(synthetic_jpeg(SCAN_BYTES))
            for number in range(1, PROGRESS_FILES):
                os.link(source, progress_root / f"photo-{number:05d}.jpg")
            for number in range(PROBE_FILES):
                os.link(FIXTURE, probe_root / f"photo-{number:03d}.jpg")

            code, stdout, stderr = invoke_tty("stats", progress_root, "--json")
            self.assertEqual(code, 0, stderr.decode(errors="replace"))
            self.assertEqual(json.loads(stdout)["scan"]["photos_parsed"],
                             PROGRESS_FILES)
            self.assertNotIn(b"\x1b", stdout)
            self.assertIn(b"\x1b[K", stderr)
            self.assertIn("Reading metadata".encode(), stderr)
            # Indeterminate form: a standalone running count, never
            # "count / total (pct%)"; the lookahead also stops a partial match.
            self.assertRegex(
                stderr.decode(errors="replace"),
                r"Reading metadata\.\.\. \d[\d,]*(?![\d,]| *[/\(%])")
            self.assertTrue(stderr.endswith(b"\r\n"), stderr[-80:])

            for args in (("--no-progress", "stats", probe_root, "--json"),
                         ("stats", probe_root, "--json", "--no-progress"),
                         ("stats", probe_root, "--json", "--quiet")):
                code, stdout, stderr = invoke_tty(*args)
                self.assertEqual(code, 0, stderr.decode(errors="replace"))
                self.assertEqual(json.loads(stdout)["scan"]["photos_parsed"],
                                 PROBE_FILES)
                self.assertNotIn(b"\x1b", stdout + stderr)
                self.assertNotIn("⠋".encode(), stderr)

            result = subprocess.run(
                [BINARY, "stats", str(probe_root), "--json"],
                capture_output=True, timeout=15, check=True)
            self.assertEqual(json.loads(result.stdout)["scan"]["photos_parsed"],
                             PROBE_FILES)
            self.assertNotIn(b"\x1b", result.stderr)

            for termination in (signal.SIGINT, signal.SIGTERM):
                code, stdout, stderr = invoke_tty(
                    "stats", progress_root, "--json",
                    interrupt_signal=termination)
                self.assertEqual(code, 1, stderr.decode(errors="replace"))
                self.assertIn(b"Interrupted", stderr)
                self.assertTrue(stderr.endswith(b"\r\n"), stderr[-80:])
                self.assertEqual(stdout, b"")


if __name__ == "__main__":
    unittest.main()

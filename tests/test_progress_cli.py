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
# Render immediately instead of waiting out the display delay, so the test does
# not depend on the scan outlasting a fixed threshold on a fast host.
os.environ["PHOTOC_PROGRESS_DELAY_MS"] = "0"


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
            # Hard links keep the fixture compact while providing enough actual
            # metadata work to pass the 200 ms display delay on supported hosts.
            for number in range(15000):
                os.link(FIXTURE, root / f"photo-{number:04d}.jpg")
            code, stdout, stderr = invoke_tty("stats", root, "--json")
            self.assertEqual(code, 0, stderr.decode(errors="replace"))
            self.assertEqual(json.loads(stdout)["scan"]["photos_parsed"], 15000)
            self.assertNotIn(b"\x1b", stdout)
            self.assertIn(b"\x1b[K", stderr)
            self.assertIn("Reading metadata".encode(), stderr)
            # Indeterminate form: a standalone running count, never
            # "count / total (pct%)"; the lookahead also stops a partial match.
            self.assertRegex(
                stderr.decode(errors="replace"),
                r"Reading metadata\.\.\. \d[\d,]*(?![\d,]| *[/\(%])")
            self.assertTrue(stderr.endswith(b"\r\n"), stderr[-80:])

            for args in (("--no-progress", "stats", root, "--json"),
                         ("stats", root, "--json", "--no-progress"),
                         ("stats", root, "--json", "--quiet")):
                code, stdout, stderr = invoke_tty(*args)
                self.assertEqual(code, 0, stderr.decode(errors="replace"))
                self.assertEqual(json.loads(stdout)["scan"]["photos_parsed"], 15000)
                self.assertNotIn(b"\x1b", stdout + stderr)
                self.assertNotIn("⠋".encode(), stderr)

            result = subprocess.run([BINARY, "stats", str(root), "--json"],
                                    capture_output=True, timeout=15, check=True)
            self.assertEqual(json.loads(result.stdout)["scan"]["photos_parsed"], 15000)
            self.assertNotIn(b"\x1b", result.stderr)

            for termination in (signal.SIGINT, signal.SIGTERM):
                code, stdout, stderr = invoke_tty(
                    "stats", root, "--json", interrupt_signal=termination)
                self.assertEqual(code, 1, stderr.decode(errors="replace"))
                self.assertIn(b"Interrupted", stderr)
                self.assertTrue(stderr.endswith(b"\r\n"), stderr[-80:])
                self.assertEqual(stdout, b"")


if __name__ == "__main__":
    unittest.main()

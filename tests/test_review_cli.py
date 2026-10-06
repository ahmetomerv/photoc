#!/usr/bin/env python3
"""PTY contract tests for the text-only review command."""

import hashlib
import json
import os
from pathlib import Path
import pty
import select
import shutil
import signal
import subprocess
import tempfile
import termios
import time
import unittest


BINARY = os.environ["PHOTOC_REVIEW_TEST_BINARY"]
FIXTURES = Path(__file__).resolve().parent / "fixtures/jpeg"


class Session:
    def __init__(self, args):
        self.master, self.slave = pty.openpty()
        self.original = termios.tcgetattr(self.slave)
        self.process = subprocess.Popen([BINARY, *map(str, args)],
                                        stdin=self.slave, stdout=self.slave,
                                        stderr=subprocess.PIPE)
        self.output = b""
        self.cursor = 0

    def read_until(self, needle, timeout=6):
        wanted = needle.encode() if isinstance(needle, str) else needle
        deadline = time.monotonic() + timeout
        while wanted not in self.output[self.cursor:] and time.monotonic() < deadline:
            ready, _, _ = select.select([self.master], [], [], 0.1)
            if ready:
                try:
                    self.output += os.read(self.master, 65536)
                except OSError:
                    break
            if self.process.poll() is not None and not ready:
                break
        if wanted not in self.output[self.cursor:]:
            raise AssertionError(f"Missing {wanted!r}; output={self.output!r}; "
                                 f"exit={self.process.poll()}")
        self.cursor = len(self.output)
        return self.output

    def send(self, keys):
        os.write(self.master, keys)

    def finish(self, keys=b"q", expected=0):
        if self.process.poll() is None:
            self.send(keys)
        code = self.process.wait(timeout=6)
        while True:
            ready, _, _ = select.select([self.master], [], [], 0)
            if not ready:
                break
            try:
                self.output += os.read(self.master, 65536)
            except OSError:
                break
        error = self.process.stderr.read().decode(errors="replace")
        assert code == expected, (code, self.output, error)
        restored = termios.tcgetattr(self.slave)
        # macOS PTYs can add EXTPROC after the child attaches to the slave.
        tracked_local = termios.ICANON | termios.ECHO | termios.ISIG | termios.IEXTEN
        assert restored[:3] == self.original[:3]
        assert restored[4:] == self.original[4:]
        assert restored[3] & tracked_local == self.original[3] & tracked_local
        return self.output.decode(errors="replace"), error

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait(timeout=6)
        self.process.stderr.close()
        os.close(self.master)
        os.close(self.slave)


class ReviewTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="photoc-review-cli-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def photo(self, name, fixture="flat.jpg"):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(FIXTURES / fixture, path)
        return path

    def run_session(self, *args):
        session = Session(["review", self.root, "--images", "none", *args])
        self.addCleanup(session.close)
        return session

    def test_requires_ttys_and_validates_options(self):
        for arguments, code, message in [
            (["review", self.root, "--images", "none"], 1, "requires a terminal"),
            (["review"], 2, "expected exactly one directory"),
            (["review", self.root, "extra"], 2, "expected exactly one directory"),
            (["review", self.root, "--json"], 2, "--json is not supported"),
            (["review", self.root, "--sort", "bad"], 2, "invalid sort"),
            (["review", self.root, "--show", "bad"], 2, "invalid show"),
            (["review", self.root, "--state"], 2, "requires a value"),
            (["review", self.root, "--images", "iterm"], 2, "only --images none"),
            (["--sort", "date", "review", self.root, "--images", "none"],
             1, "requires a terminal"),
        ]:
            with self.subTest(arguments=arguments):
                result = subprocess.run([BINARY, *map(str, arguments)],
                                        capture_output=True, timeout=6)
                self.assertEqual(result.returncode, code, result.stderr)
                self.assertIn(message.encode(), result.stderr)

    def test_navigation_marks_resume_and_jpegs_unchanged(self):
        first = self.photo("a.jpg")
        second = self.photo("b.jpeg", "sharp.jpg")
        hashes = [hashlib.sha256(p.read_bytes()).digest() for p in (first, second)]
        session = self.run_session()
        session.read_until(b"a.jpg")
        session.send(b"?")
        session.read_until(b"Help")
        session.send(b"?")
        session.send(b"i")
        session.read_until(b"Details")
        session.send(b"p")
        session.read_until(b"b.jpeg")
        session.send(b"x")
        output, error = session.finish()
        self.assertEqual(error, "")
        self.assertIn("1 picked, 1 rejected", output)
        state = json.loads((self.root / ".photoc-review.json").read_text())
        self.assertEqual({item["path"]: item["status"] for item in state["items"]},
                         {"a.jpg": "picked", "b.jpeg": "rejected"})
        self.assertEqual(hashes, [hashlib.sha256(p.read_bytes()).digest()
                                  for p in (first, second)])

        resumed = self.run_session("--show", "picked")
        resumed.read_until(b"a.jpg")
        self.assertIn(b"PICKED", resumed.output)
        resumed.finish()

    def test_arrows_filter_and_unmark(self):
        self.photo("a.jpg")
        self.photo("b.jpg")
        session = self.run_session()
        session.read_until(b"a.jpg")
        session.send(b"\x1b[C")
        session.read_until(b"b.jpg")
        session.send(b"\x1b[D")
        session.read_until(b"a.jpg")
        session.send(b"p")
        session.read_until(b"b.jpg")
        session.finish()

        filtered = self.run_session("--show", "unmarked")
        filtered.read_until(b"b.jpg")
        filtered.send(b"p")
        filtered.read_until(b"No unmarked photos")
        filtered.finish()
        self.assertEqual(len(json.loads((self.root / ".photoc-review.json").read_text())["items"]), 2)

        picked = self.run_session("--show", "picked")
        picked.read_until(b"a.jpg")
        picked.send(b"u")
        picked.read_until(b"b.jpg")
        picked.finish()
        state = json.loads((self.root / ".photoc-review.json").read_text())
        self.assertEqual([item["path"] for item in state["items"]], ["b.jpg"])

    def test_empty_and_empty_filter_do_not_enter_raw_mode(self):
        empty = self.run_session()
        output, error = empty.finish(keys=b"", expected=0)
        self.assertIn("No JPEG photos", output)
        self.assertNotIn("\x1b[?1049h", output)
        self.assertEqual(error, "")
        self.photo("a.jpg")
        filtered = self.run_session("--show", "picked")
        output, _ = filtered.finish(keys=b"", expected=0)
        self.assertIn("No picked photos", output)
        self.assertNotIn("\x1b[?1049h", output)

    def test_recursive_and_safe_filename_display(self):
        self.photo("nested/evil\x1b[31m.jpg")
        self.photo("top.jpg")
        flat = self.run_session()
        flat.read_until(b"top.jpg")
        output, _ = flat.finish()
        self.assertNotIn("evil", output)
        recursive = self.run_session("--recursive")
        recursive.read_until(b"nested/evil\\x1B[31m.jpg")
        recursive.finish()

    def test_custom_state_path_and_resize(self):
        self.photo("a.jpg")
        state_path = self.root / "selection.json"
        session = self.run_session("--state", state_path)
        session.read_until(b"a.jpg")
        os.kill(session.process.pid, signal.SIGWINCH)
        session.read_until(b"photoc review")
        session.send(b"p")
        session.finish()
        self.assertTrue(state_path.is_file())
        self.assertFalse((self.root / ".photoc-review.json").exists())

    def test_sigterm_and_sighup_restore_terminal(self):
        self.photo("a.jpg")
        for signal_number in (signal.SIGTERM, signal.SIGHUP):
            with self.subTest(signal_number=signal_number):
                session = self.run_session()
                session.read_until(b"a.jpg")
                os.kill(session.process.pid, signal_number)
                _, error = session.finish(keys=b"", expected=1)
                self.assertTrue("interrupted" in error.lower() or
                                "disconnected" in error.lower(), error)

    def test_output_failure_restores_input_terminal(self):
        self.photo("a.jpg")
        input_master, input_slave = pty.openpty()
        output_master, output_slave = pty.openpty()
        original = termios.tcgetattr(input_slave)
        process = subprocess.Popen([BINARY, "review", str(self.root),
                                    "--images", "none"], stdin=input_slave,
                                   stdout=output_slave, stderr=subprocess.PIPE)
        try:
            ready, _, _ = select.select([output_master], [], [], 6)
            self.assertTrue(ready)
            self.assertIn(b"photoc review", os.read(output_master, 65536))
            os.close(output_master)
            output_master = -1
            os.write(input_master, b"i")
            self.assertEqual(process.wait(timeout=6), 1)
            restored = termios.tcgetattr(input_slave)
            tracked = termios.ICANON | termios.ECHO | termios.ISIG | termios.IEXTEN
            self.assertEqual(restored[3] & tracked, original[3] & tracked)
            self.assertEqual(restored[6], original[6])
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=6)
            process.stderr.close()
            os.close(input_master)
            os.close(input_slave)
            if output_master >= 0:
                os.close(output_master)
            os.close(output_slave)

    def test_save_failure_keeps_mark_and_restores_terminal(self):
        self.photo("a.jpg")
        state_dir = self.root / "state"
        state_dir.mkdir()
        state_path = state_dir / "review.json"
        session = self.run_session("--state", state_path)
        session.read_until(b"a.jpg")
        state_dir.chmod(0o500)
        try:
            session.send(b"p")
            session.read_until(b"Could not save")
            output, _ = session.finish(expected=1)
            last_screen = output.rsplit("\x1b[H\x1b[2J", 1)[-1]
            self.assertIn("UNMARKED", last_screen)
            self.assertIn("Review ended with unsaved selections.", output)
            self.assertFalse(state_path.exists())
        finally:
            state_dir.chmod(0o700)

    def test_sigint_restores_terminal(self):
        self.photo("a.jpg")
        session = self.run_session()
        session.read_until(b"a.jpg")
        os.kill(session.process.pid, signal.SIGINT)
        output, error = session.finish(keys=b"", expected=1)
        self.assertIn("interrupted", error.lower())
        self.assertNotIn("Traceback", output)


if __name__ == "__main__":
    unittest.main()

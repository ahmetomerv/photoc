#!/usr/bin/env python3
"""PTY contract tests for the interactive review command."""

import base64
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
    def __init__(self, args, env=None):
        self.master, self.slave = pty.openpty()
        self.original = termios.tcgetattr(self.slave)
        self.process = subprocess.Popen([BINARY, *map(str, args)],
                                        stdin=self.slave, stdout=self.slave,
                                        stderr=subprocess.PIPE, env=env)
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
        self.cursor = self.output.find(wanted, self.cursor) + len(wanted)
        return self.output

    def send(self, keys):
        os.write(self.master, keys)

    def latest_screen(self):
        return self.output.rsplit(b"\x1b[H\x1b[2J", 1)[-1]

    def wait_screen(self):
        self.read_until(b"\x1b[H\x1b[2J")
        self.read_until(b"[Q] Quit")
        return self.latest_screen()

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
            (["review", self.root, "--images", "bad"], 2, "invalid images"),
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
        self.assertIn(b"PICKED", resumed.wait_screen())
        resumed.finish()

    def test_image_modes_and_auto_detection(self):
        self.photo("a.jpg", "flat.jpg")
        unknown = os.environ.copy()
        for name in ("TERM_PROGRAM", "ITERM_SESSION_ID", "TMUX", "STY"):
            unknown.pop(name, None)

        for options in ([], ["--images", "auto"], ["--images", "none"]):
            with self.subTest(options=options):
                session = Session(["review", self.root, *options], env=unknown)
                self.addCleanup(session.close)
                screen = session.wait_screen()
                self.assertIn(b"Sharpness:", screen)
                self.assertNotIn(b"\x1b]1337;File=", screen)
                session.finish()

        forced = Session(["review", self.root, "--images", "iterm"], env=unknown)
        self.addCleanup(forced.close)
        screen = forced.wait_screen()
        self.assertIn(b"\x1b]1337;File=inline=1;", screen)
        self.assertIn(b"preserveAspectRatio=1:", screen)
        self.assertIn(b"\a\r\n", screen)
        begin = screen.index(b"\x1b]1337;File=")
        payload_start = screen.index(b":", begin) + 1
        payload_end = screen.index(b"\a", payload_start)
        self.assertEqual(base64.b64decode(screen[payload_start:payload_end],
                                          validate=True),
                         (FIXTURES / "flat.jpg").read_bytes())
        forced.finish()

        detected = dict(unknown, TERM_PROGRAM="iTerm.app",
                        ITERM_SESSION_ID="test-session", TERM="xterm-256color")
        direct = Session(["review", self.root], env=detected)
        self.addCleanup(direct.close)
        self.assertIn(b"\x1b]1337;File=", direct.wait_screen())
        direct.finish()

        multiplexed = Session(["review", self.root], env=dict(detected, TMUX="1"))
        self.addCleanup(multiplexed.close)
        self.assertNotIn(b"\x1b]1337;File=", multiplexed.wait_screen())
        multiplexed.finish()

    def test_action_shortcuts_are_visible_with_and_without_color(self):
        self.photo("a.jpg", "flat.jpg")
        colored_env = os.environ.copy()
        colored_env.pop("NO_COLOR", None)
        colored_env["TERM"] = "xterm-256color"
        for mode in ("none", "iterm"):
            with self.subTest(mode=mode):
                session = Session(["review", self.root, "--images", mode],
                                  env=colored_env)
                self.addCleanup(session.close)
                screen = session.wait_screen()
                self.assertIn(b"ACTIONS", screen)
                self.assertIn(b"\x1b[1;30;42m[P]\x1b[0m", screen)
                self.assertIn(b"\x1b[1;37;41m[X]\x1b[0m", screen)
                self.assertIn(b"\x1b[1;30;43m[U]\x1b[0m", screen)
                self.assertIn(b"[Left/H] Previous", screen)
                self.assertIn(b"[Q] Quit", screen)
                session.finish()

        plain_env = dict(colored_env, NO_COLOR="1")
        plain = Session(["review", self.root, "--images", "none"],
                        env=plain_env)
        self.addCleanup(plain.close)
        screen = plain.wait_screen()
        self.assertIn(b"ACTIONS  [P] PICK    [X] REJECT    [U] UNMARK", screen)
        self.assertNotIn(b"\x1b[1;30;42m", screen)
        plain.finish()

    def test_iterm_navigation_and_resize_redraw(self):
        self.photo("a.jpg", "flat.jpg")
        self.photo("b.jpg", "sharp.jpg")
        unknown = os.environ.copy()
        unknown.pop("TERM_PROGRAM", None)
        unknown.pop("ITERM_SESSION_ID", None)
        session = Session(["review", self.root, "--images", "iterm"], env=unknown)
        self.addCleanup(session.close)
        self.assertIn(b"a.jpg", session.wait_screen())
        session.send(b"l")
        second = session.wait_screen()
        self.assertIn(b"b.jpg", second)
        self.assertIn(b"\x1b]1337;File=", second)
        os.kill(session.process.pid, signal.SIGWINCH)
        resized = session.wait_screen()
        self.assertIn(b"b.jpg", resized)
        self.assertIn(b"\x1b]1337;File=", resized)
        session.finish()

    def test_forced_iterm_keeps_text_when_photo_disappears(self):
        self.photo("a.jpg", "flat.jpg")
        target = self.photo("b.jpg", "sharp.jpg")
        session = Session(["review", self.root, "--images", "iterm"])
        self.addCleanup(session.close)
        self.assertIn(b"\x1b]1337;File=", session.wait_screen())
        target.unlink()
        session.send(b"l")
        screen = session.wait_screen()
        self.assertIn(b"b.jpg", screen)
        self.assertIn(b"Photo unavailable", screen)
        self.assertNotIn(b"\x1b]1337;File=", screen)
        session.send(b"h")
        self.assertIn(b"a.jpg", session.wait_screen())
        session.finish()

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

    def test_lone_escape_does_not_consume_next_shortcut(self):
        self.photo("a.jpg")
        session = self.run_session()
        session.wait_screen()
        session.send(b"\x1b")
        time.sleep(0.2)
        session.finish(keys=b"q")

    def test_uppercase_keycaps_accept_uppercase_keys(self):
        self.photo("a.jpg")
        self.photo("b.jpg")
        session = self.run_session()
        self.assertIn(b"a.jpg", session.wait_screen())
        session.send(b"P")
        self.assertIn(b"b.jpg", session.wait_screen())
        session.send(b"X")
        self.assertIn(b"REJECTED", session.wait_screen())
        session.send(b"H")
        self.assertIn(b"a.jpg", session.wait_screen())
        session.send(b"L")
        self.assertIn(b"b.jpg", session.wait_screen())
        session.send(b"U")
        self.assertIn(b"UNMARKED", session.wait_screen())
        session.send(b"I")
        self.assertIn(b"Details", session.wait_screen())
        session.finish(keys=b"Q")
        state = json.loads((self.root / ".photoc-review.json").read_text())
        self.assertEqual(state["items"], [{"path": "a.jpg", "status": "picked"}])

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

    def test_compact_and_detailed_metadata_and_missing_fields(self):
        self.photo("a.jpg", "with_exif.jpg")
        self.photo("b.jpg", "no_exif.jpg")
        session = self.run_session()
        compact = session.wait_screen()
        self.assertIn(b"Fixture Camera Co. Model Z", compact)
        self.assertIn(b"3 x 2", compact)
        self.assertIn(b"1/125s", compact)
        self.assertIn(b"f/2.8", compact)
        self.assertIn(b"ISO 200", compact)
        self.assertIn(b"50mm", compact)
        self.assertIn(b"2026:09:27 12:34:56", compact)
        self.assertNotIn(b"Details", compact)
        session.send(b"i")
        detailed = session.wait_screen()
        self.assertIn(b"Details", detailed)
        self.assertIn(b"Camera: Fixture Camera Co. Model Z", detailed)
        self.assertIn(b"Dimensions: 3 x 2", detailed)
        self.assertIn(b"Exposure: 1/125s", detailed)
        session.send(b"i")
        self.assertNotIn(b"Details", session.wait_screen())
        session.send(b"l")
        missing = session.wait_screen()
        self.assertIn(b"b.jpg", missing)
        self.assertIn(b"3 x 2", missing)
        self.assertNotIn(b"Camera:", missing)
        self.assertNotIn(b"ISO", missing)
        self.assertNotIn(b"Captured:", missing)
        session.finish()

    def test_sharpness_success_and_failure_are_cached(self):
        first = self.photo("a.jpg", "flat.jpg")
        second = self.photo("b.jpg", "invalid.jpg")
        self.photo("c.jpg", "sharp.jpg")
        session = self.run_session()
        self.assertIn(b"Sharpness: 0.000", session.wait_screen())
        session.send(b"l")
        self.assertIn(b"Sharpness: unavailable", session.wait_screen())
        shutil.copyfile(FIXTURES / "sharp.jpg", second)
        session.send(b"l")
        self.assertIn(b"c.jpg", session.wait_screen())
        shutil.copyfile(FIXTURES / "sharp.jpg", first)
        session.send(b"h")
        self.assertIn(b"Sharpness: unavailable", session.wait_screen())
        session.send(b"h")
        self.assertIn(b"Sharpness: 0.000", session.wait_screen())
        session.finish()
        self.assertFalse((self.root / ".photoc-review.json").exists())

    def test_disappearing_and_unreadable_photo_keeps_navigation_and_state(self):
        self.photo("a.jpg", "flat.jpg")
        target = self.photo("b.jpg", "sharp.jpg")
        session = self.run_session()
        self.assertIn(b"a.jpg", session.wait_screen())
        target.unlink()
        session.send(b"l")
        self.assertIn(b"Photo unavailable", session.wait_screen())
        session.send(b"p")
        self.assertIn(b"PICKED", session.wait_screen())
        session.send(b"h")
        self.assertIn(b"a.jpg", session.wait_screen())
        target.write_bytes(b"not a JPEG")
        session.send(b"l")
        unreadable = session.wait_screen()
        self.assertIn(b"Photo unavailable", unreadable)
        self.assertIn(b"Sharpness: unavailable", unreadable)
        session.finish()
        state = json.loads((self.root / ".photoc-review.json").read_text())
        self.assertEqual(state["items"], [{"path": "b.jpg", "status": "picked"}])

    def test_missing_photo_caches_sharpness_failure(self):
        self.photo("a.jpg", "flat.jpg")
        target = self.photo("b.jpg", "sharp.jpg")
        session = self.run_session()
        session.wait_screen()
        target.unlink()
        session.send(b"l")
        self.assertIn(b"Photo unavailable", session.wait_screen())
        session.send(b"h")
        session.wait_screen()
        shutil.copyfile(FIXTURES / "sharp.jpg", target)
        session.send(b"l")
        self.assertIn(b"Sharpness: unavailable", session.wait_screen())
        session.finish()

    def test_filename_and_metadata_control_bytes_are_escaped(self):
        original = (FIXTURES / "with_exif.jpg").read_bytes()
        bad_make = b"evil\x1b[31m\tname\n".ljust(18, b"X")
        self.assertEqual(len(bad_make), 18)
        data = original.replace(b"Fixture Camera Co.", bad_make)
        self.assertNotEqual(data, original)
        path = self.root / "bad\tname\nevil\x1b[31m.jpg"
        path.write_bytes(data)
        session = self.run_session()
        screen = session.wait_screen()
        self.assertIn(b"bad\\x09name\\x0Aevil\\x1B[31m.jpg", screen)
        self.assertIn(b"evil\\x1B[31m\\x09name\\x0A", screen)
        self.assertNotIn(b"bad\tname\n", screen)
        self.assertNotIn(b"evil\x1b[31m", screen)
        session.finish()


if __name__ == "__main__":
    unittest.main()

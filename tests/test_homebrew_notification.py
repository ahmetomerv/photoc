#!/usr/bin/env python3
"""Exercise release validation and the cross-repository dispatch request."""

import importlib.util
import io
import json
from pathlib import Path
import unittest
from urllib.request import Request


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "notify_homebrew_tap", ROOT / "scripts/notify-homebrew-tap.py")
notification = importlib.util.module_from_spec(spec)
spec.loader.exec_module(notification)


class Response(io.BytesIO):
    def __init__(self, body=b"", status=200):
        super().__init__(body)
        self.status = status


class HomebrewNotificationTests(unittest.TestCase):
    def test_published_release_dispatches_only_the_tag(self):
        requests = []

        def open_request(request, timeout):
            self.assertIsInstance(request, Request)
            self.assertEqual(timeout, 20)
            requests.append(request)
            if len(requests) == 1:
                return Response(json.dumps({
                    "tag_name": "v0.5.1", "draft": False, "prerelease": False,
                }).encode("utf-8"))
            return Response(status=204)

        notification.notify("v0.5.1", "source-secret", "tap-secret", opener=open_request)
        self.assertEqual(len(requests), 2)
        self.assertEqual(requests[0].full_url,
                         "https://api.github.com/repos/ahmetomerv/photoc/releases/tags/v0.5.1")
        self.assertEqual(requests[0].get_header("Authorization"), "Bearer source-secret")
        self.assertEqual(requests[1].full_url,
                         "https://api.github.com/repos/ahmetomerv/homebrew-photoc/dispatches")
        self.assertEqual(requests[1].get_header("Authorization"), "Bearer tap-secret")
        self.assertEqual(json.loads(requests[1].data), {
            "event_type": "photoc_release_published",
            "client_payload": {"tag": "v0.5.1"},
        })

    def test_invalid_tag_or_missing_token_never_calls_github(self):
        def unexpected_request(_request, timeout):
            self.fail(f"unexpected request with timeout {timeout}")

        for tag, source_token, tap_token in (
                ("v0.5.1; echo unsafe", "source", "tap"),
                ("v01.5.1", "source", "tap"),
                ("v0.5.1", "", "tap"),
                ("v0.5.1", "source", "")):
            with self.subTest(tag=tag, source_token=bool(source_token), tap_token=bool(tap_token)):
                with self.assertRaises(ValueError):
                    notification.notify(tag, source_token, tap_token, opener=unexpected_request)

    def test_unpublished_release_never_dispatches(self):
        for release in (
                {"tag_name": "v0.5.1", "draft": True, "prerelease": False},
                {"tag_name": "v0.5.1", "draft": False, "prerelease": True},
                {"tag_name": "v0.5.2", "draft": False, "prerelease": False},
                {"tag_name": "v0.5.1", "draft": False}):
            with self.subTest(release=release):
                requests = []

                def open_request(request, timeout):
                    self.assertEqual(timeout, 20)
                    requests.append(request)
                    return Response(json.dumps(release).encode("utf-8"))

                with self.assertRaises(ValueError):
                    notification.notify("v0.5.1", "source", "tap", opener=open_request)
                self.assertEqual(len(requests), 1)


if __name__ == "__main__":
    unittest.main()

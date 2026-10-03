#!/usr/bin/env python3
"""Request a Homebrew tap update after a stable photoc release is published."""

import argparse
import json
import os
import re
import sys
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


SOURCE_REPO = "ahmetomerv/photoc"
TAP_REPO = "ahmetomerv/homebrew-photoc"
EVENT_TYPE = "photoc_release_published"
API_ROOT = "https://api.github.com"
API_HEADERS = {
    "Accept": "application/vnd.github+json",
    "User-Agent": "photoc-release-workflow",
    "X-GitHub-Api-Version": "2022-11-28",
}
TAG_PATTERN = re.compile(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z")


def github_request(path, token, *, payload=None, opener=urlopen):
    """Send one GitHub API request; the caller owns the returned JSON value."""
    headers = {**API_HEADERS, "Authorization": f"Bearer {token}"}
    data = None
    if payload is not None:
        headers["Content-Type"] = "application/json"
        data = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    request = Request(API_ROOT + path, data=data, headers=headers)
    with opener(request, timeout=20) as response:
        if payload is not None:
            if response.status != 204:
                raise ValueError(f"GitHub dispatch returned HTTP {response.status}, expected 204")
            return None
        return json.load(response)


def notify(tag, source_token, tap_token, *, opener=urlopen):
    """Dispatch exactly one update request for a published stable release."""
    if not TAG_PATTERN.fullmatch(tag):
        raise ValueError("tag must be vMAJOR.MINOR.PATCH without leading zeroes")
    if not source_token or not tap_token:
        raise ValueError("source and tap GitHub tokens must both be configured")

    release = github_request(
        f"/repos/{SOURCE_REPO}/releases/tags/{tag}", source_token, opener=opener)
    if (not isinstance(release, dict) or release.get("tag_name") != tag
            or release.get("draft") is not False
            or release.get("prerelease") is not False):
        raise ValueError(f"{tag} is not a published stable photoc release")

    github_request(
        f"/repos/{TAP_REPO}/dispatches", tap_token,
        payload={"event_type": EVENT_TYPE, "client_payload": {"tag": tag}}, opener=opener)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True, help="published stable photoc tag, for example v0.5.1")
    args = parser.parse_args()
    try:
        notify(args.tag, os.environ.get("PHOTOC_SOURCE_GITHUB_TOKEN"),
               os.environ.get("PHOTOC_TAP_DISPATCH_TOKEN"))
    except (ValueError, HTTPError, URLError, OSError) as error:
        parser.exit(1, f"homebrew notification: {error}\n")
    print(f"Requested Homebrew tap update for {args.tag}")


if __name__ == "__main__":
    main()

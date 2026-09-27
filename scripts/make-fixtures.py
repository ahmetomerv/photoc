#!/usr/bin/env python3
"""Regenerate all generated binary fixtures under tests/fixtures/.

Runs the metadata generator (stdlib only; uses cjpeg when available) and the
sharpness generator (requires cjpeg). See tests/fixtures/README.md.
"""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys


SCRIPTS = Path(__file__).resolve().parent


def load(name: str):
    path = SCRIPTS / name
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    load("make-metadata-fixtures.py").main()
    try:
        load("make-sharpness-fixtures.py").main()
    except FileNotFoundError as error:
        print(f"sharpness fixtures need cjpeg: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

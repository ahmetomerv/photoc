#!/usr/bin/env python3
"""Warm-cache CLI baselines for the image-processing commands.

Times `photoc focus`, `check`, `compress`, and `contact` against a synthetic
JPEG corpus generated with `cjpeg` from libjpeg-turbo -- the same optional
benchmark-only tool `benchmark-concurrency.py` uses. No user photos are read
or changed; every pixel is procedurally generated.

Each workload runs once as a warm-up and then `--trials` timed runs. The
reported elapsed time is the median, so a cold-cache outlier cannot hide a
regression. Peak RSS is the largest child value across the timed runs. This
complements `benchmark.py` (stats/duplicates) and `benchmark-concurrency.py`
(core worker scaling).

Build the CLI first:

  cmake -S . -B build && cmake --build build
  python3 scripts/benchmark-image.py
  python3 scripts/benchmark-image.py --count 256 --trials 5 --json

Usage:
  python3 scripts/benchmark-image.py [--photoc PATH] [--count N]
      [--size WIDTH] [--quality N] [--target-size auto|SIZE] [--trials N]
      [--data-dir DIR] [--keep] [--json]
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import sys
from typing import Callable

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DATA = ROOT / "benchmarks" / "data-image"

# Tiny helper that execs photoc as a child so RUSAGE_CHILDREN is unambiguous.
_RUNNER = r"""
import resource
import subprocess
import sys
import time

command = sys.argv[1:]
start = time.perf_counter()
completed = subprocess.run(command)
elapsed = time.perf_counter() - start
usage = resource.getrusage(resource.RUSAGE_CHILDREN)
print(
    f"PHOTOC_BENCH exit={completed.returncode} elapsed={elapsed:.6f} "
    f"maxrss={usage.ru_maxrss}",
    file=sys.stderr,
)
raise SystemExit(completed.returncode)
"""


def find_photoc(explicit: str | None) -> Path:
    if explicit:
        path = Path(explicit)
    else:
        candidates = [
            ROOT / "build" / "photoc",
            ROOT / "build" / "Release" / "photoc",
            ROOT / "build" / "Debug" / "photoc",
        ]
        path = next((candidate for candidate in candidates if candidate.is_file()),
                    candidates[0])
    if not path.is_file():
        raise SystemExit(f"photoc binary not found at {path}; build it first")
    return path.resolve()


def find_cjpeg() -> Path | None:
    found = shutil.which("cjpeg")
    if found:
        return Path(found)
    for candidate in ("/opt/homebrew/opt/jpeg-turbo/bin/cjpeg",
                      "/usr/local/opt/jpeg-turbo/bin/cjpeg"):
        if Path(candidate).is_file():
            return Path(candidate)
    return None


def render_ppm(width: int, height: int, phase: int) -> bytes:
    """A high-frequency color checkerboard with a gradient detail term.

    The detail keeps the image from compressing trivially, so `compress
    --target` actually has to search quality instead of ending at quality 100.
    """
    header = f"P6\n{width} {height}\n255\n".encode("ascii")
    body = bytearray(width * height * 3)
    at = 0
    for y in range(height):
        cell_y = (y // 16) & 1
        for x in range(width):
            detail = (x * 5 + y * 3 + phase * 29) % 192
            value = 32 + detail
            if ((x + phase * 16) // 16 + cell_y) & 1:
                value = 224 - (detail % 48)
            value &= 0xFF
            body[at] = value
            body[at + 1] = (value * 2 + 13) & 0xFF
            body[at + 2] = (value * 3 + 71) & 0xFF
            at += 3
    return header + bytes(body)


def encode_jpeg(cjpeg: Path, ppm: bytes, quality: int) -> bytes:
    return subprocess.run([str(cjpeg), "-quality", str(quality), "-optimize"],
                          input=ppm, capture_output=True, check=True).stdout


def materialize(root: Path, count: int, images: list[bytes],
                batch: int = 32) -> int:
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    for index in range(count):
        directory = root / f"batch_{index // batch:03d}"
        directory.mkdir(exist_ok=True)
        (directory / f"photo_{index:05d}.jpg").write_bytes(
            images[index % len(images)])
    return count


def normalize_maxrss(raw: int) -> int:
    """Return peak RSS in bytes. Linux reports KiB; macOS reports bytes."""
    if sys.platform == "darwin":
        return raw
    return raw * 1024


def run_once(photoc: Path, args: list[str]) -> tuple[float, int, int, str]:
    command = [str(photoc), *args]
    runner = [sys.executable, "-c", _RUNNER, *command]
    completed = subprocess.run(runner, capture_output=True, text=True)
    marker = None
    for line in completed.stderr.splitlines():
        if line.startswith("PHOTOC_BENCH "):
            marker = line
    if marker is None:
        return 0.0, 0, -1, completed.stderr
    parts = dict(item.split("=", 1) for item in marker.split()[1:])
    return (float(parts["elapsed"]), normalize_maxrss(int(parts["maxrss"])),
            int(parts["exit"]), completed.stderr)


def measure(photoc: Path, args: list[str], label: str, file_count: int,
            trials: int,
            prepare: Callable[[], None] | None = None) -> dict:
    # One warm-up per workload, excluded from the median.
    if prepare is not None:
        prepare()
    _, _, warm_exit, warm_output = run_once(photoc, args)
    if warm_exit != 0:
        sys.stderr.write(warm_output)
        raise SystemExit(f"{label}: warm-up failed with exit {warm_exit}")

    elapsed_samples: list[float] = []
    peak_rss = 0
    for _ in range(trials):
        if prepare is not None:
            prepare()
        elapsed, rss, exit_code, output = run_once(photoc, args)
        if exit_code != 0:
            sys.stderr.write(output)
            raise SystemExit(f"{label} failed with exit {exit_code}")
        elapsed_samples.append(elapsed)
        peak_rss = max(peak_rss, rss)

    elapsed = statistics.median(elapsed_samples)
    files_per_sec = file_count / elapsed if elapsed > 0 else float("inf")
    return {
        "label": label,
        "command": " ".join([str(photoc), *args]),
        "files": file_count,
        "trials": trials,
        "elapsed_s": elapsed,
        "files_per_sec": files_per_sec,
        "peak_rss_bytes": peak_rss,
    }


def format_bytes(value: int | None) -> str:
    if value is None:
        return "n/a"
    if value >= 1024 * 1024:
        return f"{value / (1024 * 1024):.1f} MiB"
    if value >= 1024:
        return f"{value / 1024:.1f} KiB"
    return f"{value} B"


def print_result(result: dict) -> None:
    print(f"\n== {result['label']} ==")
    print(f"command:      {result['command']}")
    print(f"files:        {result['files']}")
    print(f"trials:       {result['trials']} (median)")
    print(f"elapsed:      {result['elapsed_s']:.3f} s")
    print(f"throughput:   {result['files_per_sec']:.1f} files/s")
    print(f"peak RSS:     {format_bytes(result['peak_rss_bytes'])}")


def write_markdown_row(result: dict) -> str:
    return (f"| `{result['label']}` | {result['files']} | "
            f"{result['elapsed_s']:.3f} | {result['files_per_sec']:.1f} | "
            f"{format_bytes(result['peak_rss_bytes'])} |")


def clean_dir(directory: Path) -> None:
    shutil.rmtree(directory, ignore_errors=True)


def make_contact_prepare(directory: Path) -> Callable[[], None]:
    def prepare() -> None:
        shutil.rmtree(directory, ignore_errors=True)
        directory.mkdir(parents=True, exist_ok=True)
    return prepare


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--photoc", help="Path to the photoc binary")
    parser.add_argument("--data-dir", type=Path, default=DEFAULT_DATA,
                        help="Directory for generated corpora "
                             "(default: benchmarks/data-image)")
    parser.add_argument("--count", type=int, default=128,
                        help="Photos in the focus/check corpus (default: 128)")
    parser.add_argument("--size", type=int, default=1280,
                        help="Corpus width in pixels; height is 3/4 "
                             "(default: 1280)")
    parser.add_argument("--quality", type=int, default=85,
                        help="Source JPEG quality (default: 85)")
    parser.add_argument("--target-size", default="auto",
                        help="Target for the compress size-search baseline, "
                             "or 'auto' for half the source JPEG size "
                             "(default: auto)")
    parser.add_argument("--trials", type=int, default=3,
                        help="Timed runs per workload (default: 3)")
    parser.add_argument("--keep", action="store_true",
                        help="Keep generated corpora after the run")
    parser.add_argument("--json", action="store_true",
                        help="Emit machine-readable JSON after the text report")
    args = parser.parse_args()
    if args.count < 4:
        raise SystemExit("--count must be >= 4")
    if args.size < 32 or args.size % 4 != 0:
        raise SystemExit("--size must be a multiple of 4 and >= 32")
    if not 1 <= args.quality <= 100:
        raise SystemExit("--quality must be between 1 and 100")
    if args.trials < 1:
        raise SystemExit("--trials must be >= 1")

    photoc = find_photoc(args.photoc)
    cjpeg = find_cjpeg()
    if cjpeg is None:
        raise SystemExit("cjpeg (libjpeg-turbo) is required to build the "
                         "synthetic corpus; install jpeg-turbo or add cjpeg "
                         "to PATH")
    data_dir = args.data_dir.resolve()
    if data_dir.exists():
        shutil.rmtree(data_dir)
    data_dir.mkdir(parents=True)

    width = args.size
    height = args.size * 3 // 4
    print(f"photoc:       {photoc}")
    print(f"cjpeg:        {cjpeg}")
    print(f"host:         {platform.system()} {platform.release()} "
          f"({platform.machine()})")
    print(f"data:         {data_dir}")
    print(f"image size:   {width}x{height}, source quality {args.quality}")

    images = [encode_jpeg(cjpeg, render_ppm(width, height, phase), args.quality)
              for phase in (0, 1)]
    source_bytes = statistics.median(len(image) for image in images)
    print(f"source bytes: {format_bytes(int(source_bytes))} per template "
          f"({len(images)} templates)")

    # Half the source size is normally reachable at a middle quality but not at
    # quality 100, so the binary search actually runs. An unreachable target
    # makes `compress` exit 1, so 'auto' avoids a configuration-dependent fail.
    if args.target_size == "auto":
        # The size parser takes integers only, so round down to whole KiB.
        target_label = f"{max(1, int(source_bytes // 2) // 1024)}KiB"
    else:
        target_label = args.target_size
    print(f"compress target: {target_label}")

    # Focus/check are cheap per file, so they use the full corpus. Compress and
    # contact are heavier per file; half the corpus keeps one timed run near a
    # few seconds.
    heavy_count = max(4, args.count // 2)
    photos_dir = data_dir / "photos"
    compress_src = data_dir / "compress-src"
    contact_src = data_dir / "contact-src"
    photos_files = materialize(photos_dir, args.count, images)
    compress_files = materialize(compress_src, heavy_count, images)
    contact_files = materialize(contact_src, heavy_count, images)
    print(f"focus/check:  {photos_files} photos")
    print(f"compress:     {compress_files} photos")
    print(f"contact:      {contact_files} photos")

    compress_out = data_dir / "compress-out"
    contact_out = data_dir / "contact-out"

    results = [
        measure(photoc, ["focus", "--recursive", str(photos_dir)],
                "focus --recursive", photos_files, args.trials),
        measure(photoc, ["check", "--recursive", str(photos_dir)],
                "check --recursive", photos_files, args.trials),
        measure(photoc, ["compress", "--recursive", "--quality", "80",
                         "--output-dir", str(compress_out),
                         str(compress_src)],
                "compress --quality 80", compress_files, args.trials,
                prepare=lambda: clean_dir(compress_out)),
        measure(photoc, ["compress", "--recursive", "--target",
                         target_label, "--output-dir", str(compress_out),
                         str(compress_src)],
                f"compress --target {target_label}", compress_files,
                args.trials, prepare=lambda: clean_dir(compress_out)),
        measure(photoc, ["contact", "--recursive", "--output",
                         str(contact_out / "sheet.jpg"), str(contact_src)],
                "contact --recursive", contact_files, args.trials,
                prepare=make_contact_prepare(contact_out)),
    ]
    for result in results:
        print_result(result)

    print("\nMarkdown rows:")
    print("| Command | Files | Elapsed (s) | Files/s | Peak RSS |")
    print("| --- | ---: | ---: | ---: | ---: |")
    for result in results:
        print(write_markdown_row(result))

    if args.json:
        print(json.dumps({
            "photoc": str(photoc),
            "host": {
                "system": platform.system(),
                "release": platform.release(),
                "machine": platform.machine(),
            },
            "params": {
                "count": args.count,
                "size": width,
                "quality": args.quality,
                "target_size": target_label,
                "trials": args.trials,
            },
            "results": results,
        }, indent=2))

    if not args.keep:
        shutil.rmtree(data_dir, ignore_errors=True)
    return 0


if __name__ == "__main__":
    os.environ.pop("PYTHONPATH", None)
    raise SystemExit(main())

#!/usr/bin/env python3
"""Lightweight benchmarks for `photoc stats` and `photoc duplicates`.

Builds synthetic corpora under benchmarks/data/ (gitignored), runs the built
CLI, and reports wall time, files/sec, and peak child RSS via
`resource.getrusage`. Does not change photoc behavior.

Usage:
  cmake -S . -B build && cmake --build build
  python3 scripts/benchmark.py
  python3 scripts/benchmark.py --count 5000 --photoc build/photoc
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import resource
import shutil
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DATA = ROOT / "benchmarks" / "data"
FIXTURES = ROOT / "tests" / "fixtures" / "jpeg"

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


def count_files(directory: Path) -> int:
    return sum(1 for path in directory.rglob("*") if path.is_file())


def write_unique_bytes(path: Path, size: int, seed: int) -> None:
    chunk = bytes(((seed + i) * 17 + 31) & 0xFF for i in range(256))
    repeats = (size + len(chunk) - 1) // len(chunk)
    path.write_bytes((chunk * repeats)[:size])


def prepare_stats_corpus(root: Path, count: int) -> int:
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    sources = [
        FIXTURES / "with_exif.jpg",
        FIXTURES / "with_alt_exif.jpg",
        FIXTURES / "with_gps.jpeg",
        FIXTURES / "no_exif.jpg",
    ]
    for source in sources:
        if not source.is_file():
            raise SystemExit(
                f"missing fixture {source}; run scripts/make-fixtures.py")
    for index in range(count):
        batch = root / f"batch_{index // 250:04d}"
        batch.mkdir(parents=True, exist_ok=True)
        source = sources[index % len(sources)]
        shutil.copyfile(source, batch / f"photo_{index:05d}{source.suffix}")
    return count_files(root)


def prepare_duplicates_corpus(root: Path, count: int) -> int:
    """Create mostly unique same-size files plus a few exact duplicate groups.

    Same-size unique files force SHA-256 hashing; duplicate groups exercise
    grouping. Nested directories exercise recursive walks.
    """
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    unique = max(1, count - 20)
    for index in range(unique):
        batch = root / f"batch_{index // 250:04d}"
        batch.mkdir(parents=True, exist_ok=True)
        write_unique_bytes(batch / f"unique_{index:05d}.bin", 4096, index)

    payload = b"photoc-benchmark-duplicate-payload\n" * 64
    for group in range(4):
        group_dir = root / f"dups_{group:02d}"
        group_dir.mkdir(parents=True, exist_ok=True)
        for copy in range(5):
            (group_dir / f"copy_{copy}.bin").write_bytes(payload + bytes([group]))
    return count_files(root)


def normalize_maxrss(raw: int) -> int:
    """Return peak RSS in bytes. Linux reports KiB; macOS reports bytes."""
    if sys.platform == "darwin":
        return raw
    return raw * 1024


def run_benchmark(photoc: Path, args: list[str], label: str,
                  file_count: int) -> dict:
    command = [str(photoc), *args]
    runner = [sys.executable, "-c", _RUNNER, *command]
    completed = subprocess.run(runner, capture_output=True, text=True)
    marker = None
    for line in completed.stderr.splitlines():
        if line.startswith("PHOTOC_BENCH "):
            marker = line
    if marker is None:
        sys.stderr.write(completed.stderr)
        raise SystemExit(f"{label}: missing PHOTOC_BENCH marker")

    parts = dict(item.split("=", 1) for item in marker.split()[1:])
    exit_code = int(parts["exit"])
    elapsed = float(parts["elapsed"])
    rss = normalize_maxrss(int(parts["maxrss"]))
    if exit_code != 0:
        sys.stderr.write(completed.stderr)
        raise SystemExit(f"{label} failed with exit {exit_code}")

    files_per_sec = file_count / elapsed if elapsed > 0 else float("inf")
    return {
        "label": label,
        "command": " ".join(command),
        "files": file_count,
        "elapsed_s": elapsed,
        "files_per_sec": files_per_sec,
        "peak_rss_bytes": rss,
        "exit": exit_code,
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
    print(f"elapsed:      {result['elapsed_s']:.3f} s")
    print(f"throughput:   {result['files_per_sec']:.1f} files/s")
    print(f"peak RSS:     {format_bytes(result['peak_rss_bytes'])}")


def write_markdown_row(result: dict) -> str:
    rss = format_bytes(result["peak_rss_bytes"])
    return (f"| `{result['label']}` | {result['files']} | "
            f"{result['elapsed_s']:.3f} | {result['files_per_sec']:.1f} | "
            f"{rss} |")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--photoc", help="Path to the photoc binary")
    parser.add_argument("--data-dir", type=Path, default=DEFAULT_DATA,
                        help="Directory for generated corpora "
                             "(default: benchmarks/data)")
    parser.add_argument("--count", type=int, default=1000,
                        help="Approximate files per corpus (default: 1000)")
    parser.add_argument("--keep", action="store_true",
                        help="Keep generated corpora after the run")
    parser.add_argument("--json", action="store_true",
                        help="Emit machine-readable JSON after the text report")
    args = parser.parse_args()
    if args.count < 1:
        raise SystemExit("--count must be >= 1")

    photoc = find_photoc(args.photoc)
    data_dir = args.data_dir.resolve()
    data_dir.mkdir(parents=True, exist_ok=True)
    stats_dir = data_dir / "stats"
    duplicates_dir = data_dir / "duplicates"

    print(f"photoc:       {photoc}")
    print(f"host:         {platform.system()} {platform.release()} "
          f"({platform.machine()})")
    print(f"data:         {data_dir}")
    print(f"count target: {args.count}")

    stats_files = prepare_stats_corpus(stats_dir, args.count)
    duplicates_files = prepare_duplicates_corpus(duplicates_dir, args.count)
    print(f"stats files:  {stats_files}")
    print(f"dup files:    {duplicates_files}")

    results = [
        run_benchmark(photoc, ["stats", "--recursive", str(stats_dir)],
                      "stats --recursive", stats_files),
        run_benchmark(photoc, ["duplicates", "--recursive",
                               str(duplicates_dir)],
                      "duplicates --recursive", duplicates_files),
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
            "results": results,
        }, indent=2))

    if not args.keep:
        shutil.rmtree(data_dir, ignore_errors=True)
    return 0


if __name__ == "__main__":
    os.environ.pop("PYTHONPATH", None)
    raise SystemExit(main())

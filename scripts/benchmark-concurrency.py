#!/usr/bin/env python3
"""Compare bounded workers using generated data and warm-cache medians.

Build first with PHOTOC_BENCHMARKS=ON and CMAKE_BUILD_TYPE=Release. This script
uses only temporary synthetic files; it never benchmarks user photographs.
Focus measures the existing core API, not the unimplemented CLI command.
"""
import argparse
import json
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def prepare(root):
    for name in ("metadata", "metadata-small", "hashes", "filtered", "focus"):
        (root / name).mkdir()
    fixtures = ROOT / "tests/fixtures/jpeg"
    sources = ["with_exif.jpg", "with_alt_exif.jpg", "with_gps.jpeg", "no_exif.jpg"]
    for i in range(4000):
        shutil.copyfile(fixtures / sources[i % len(sources)],
                        root / "metadata" / f"{i:05}.jpg")
    for i in range(8):
        shutil.copyfile(fixtures / sources[i % len(sources)],
                        root / "metadata-small" / f"{i:03}.jpg")
    payload = bytes(range(256)) * 8192  # 2 MiB; shared size and prefix.
    for i in range(64):
        (root / "hashes" / f"{i:04}.bin").write_bytes(payload[:-1] + bytes([i // 4]))
    for i in range(1000):
        (root / "filtered" / f"{i:04}.bin").write_bytes(
            i.to_bytes(8, "little") + payload[:4088])
    cjpeg = shutil.which("cjpeg")
    if cjpeg is None:
        for candidate in ("/opt/homebrew/opt/jpeg-turbo/bin/cjpeg",
                          "/usr/local/opt/jpeg-turbo/bin/cjpeg"):
            if Path(candidate).is_file():
                cjpeg = candidate
                break
    if cjpeg is None:
        raise SystemExit("cjpeg is required to generate the synthetic focus corpus")
    size = 2048
    pixels = bytes(230 if (x // 16 + y // 16) % 2 else 25
                   for y in range(size) for x in range(size))
    jpeg = subprocess.run([cjpeg, "-quality", "85", "-grayscale"],
                          input=f"P5\n{size} {size}\n255\n".encode() + pixels,
                          capture_output=True, check=True).stdout
    for i in range(128):
        (root / "focus" / f"{i:04}.jpg").write_bytes(jpeg)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path,
                        default=ROOT / "build-bench/benchmark_concurrency")
    parser.add_argument("--trials", type=int, default=3)
    args = parser.parse_args()
    if args.trials < 1 or not args.binary.is_file():
        parser.error("build the benchmark binary and use at least one trial")
    binary = args.binary.resolve()
    print(f"Host: {platform.platform()}; median of {args.trials} warm trials")
    print("Workload                 1 worker   2 workers  4 workers  2-worker gain")
    with tempfile.TemporaryDirectory(prefix="photoc-concurrency-bench-") as temporary:
        root = Path(temporary)
        prepare(root)
        workloads = [("metadata", "metadata"), ("metadata", "metadata-small"),
                     ("duplicates", "hashes"),
                     ("duplicates", "filtered"), ("focus", "focus")]
        for mode, corpus in workloads:
            medians = {}
            baseline = None
            for workers in (1, 2, 4):
                samples = []
                for trial in range(args.trials + 1):
                    output = subprocess.check_output(
                        [str(binary), mode, str(root / corpus), str(workers)], text=True)
                    result = json.loads(output)
                    identity = (result["items"], result["checksum"])
                    if baseline is None:
                        baseline = identity
                    if identity != baseline:
                        raise SystemExit(f"Result mismatch for {mode}/{corpus} at {workers} workers")
                    if trial > 0:  # Exclude one warm-up per worker count.
                        samples.append(result["seconds"])
                medians[workers] = statistics.median(samples)
            label = f"{mode}/{corpus}"
            print(f"{label:24} {medians[1]:.4f}s    {medians[2]:.4f}s    "
                  f"{medians[4]:.4f}s    {medians[1] / medians[2]:.2f}x")


if __name__ == "__main__":
    main()

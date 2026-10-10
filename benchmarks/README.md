# Benchmarks

Lightweight wall-clock measurements for `photoc stats`, `duplicates`,
`focus`, `check`, `compress`, and `contact`. These runs generate synthetic
corpora, time the built CLI, and report files/sec plus peak child RSS. They
are for regression awareness and bottleneck notes — not a formal performance
suite.

There are three scripts:

- `benchmark.py` — metadata-only commands (`stats`, `duplicates`), single run.
- `benchmark-image.py` — image commands (`focus`, `check`, `compress`,
  `contact`), warm-cache medians; needs `cjpeg` from libjpeg-turbo.
- `benchmark-concurrency.py` — core-API worker scaling, needs `cjpeg` and a
  `PHOTOC_BENCHMARKS=ON` Release build.

## Running

```sh
cmake -S . -B build && cmake --build build
python3 scripts/benchmark.py
python3 scripts/benchmark.py --count 5000 --keep
python3 scripts/benchmark-image.py
python3 scripts/benchmark-image.py --count 256 --trials 5 --json
```

| Flag | Meaning |
| --- | --- |
| `--count N` | Approximate files per corpus (default `1000`) |
| `--photoc PATH` | Binary to measure (default `build/photoc`) |
| `--data-dir DIR` | Corpus root (default `benchmarks/data`, gitignored) |
| `--keep` | Leave the generated trees after the run |
| `--json` | Also print a JSON blob after the text report |

`benchmark-image.py` accepts the same flags plus `--size`, `--quality`,
`--target-size`, and `--trials`; see its `--help`. It defaults to a separate
`benchmarks/data-image` corpus root, so the two scripts do not delete each
other's data.

CI runs both scripts with a tiny corpus on Linux and macOS to keep the harness
working; the timings there are not tracked, and the numbers below come from a
warm-cache local run.

The metadata script rebuilds corpora each run:

- **stats** — copies of the small synthetic JPEG fixtures under nested
  `batch_*/` directories (EXIF / alternate EXIF / GPS / no-EXIF).
- **duplicates** — many unique 4 KiB files of the same size (exercises the
  same-size path) plus a few exact duplicate groups.

## Baseline results

Recorded on 2026-09-27 with a local build (`cmake --build build`, no
sanitizers) after the duplicates optimizations below:

| Host | Notes |
| --- | --- |
| Darwin 27.0.0 arm64 | Apple Silicon laptop, warm disk cache |

| Command | Files | Elapsed (s) | Files/s | Peak RSS |
| --- | ---: | ---: | ---: | ---: |
| `stats --recursive` | 1000 | 0.045 | 22062 | 6.3 MiB |
| `duplicates --recursive` | 1000 | 0.077 | 13049 | ~8–10 MiB |
| `stats --recursive` | 5000 | 0.224 | 22366 | 6.3 MiB |
| `duplicates --recursive` | 5000 | 0.444 | 11251 | 8.0 MiB |

Additional duplicates stress (not produced by `benchmark.py`): **8020** files of
**64 KiB**, almost all unique content with distinct leading bytes, plus five
exact duplicate groups of four copies:

| Build | Elapsed | Files hashed | Notes |
| --- | ---: | ---: | --- |
| Before prefix filter | ~7.6 s | 8020 | Full SHA-256 of every same-size file |
| After optimizations | ~0.16 s | 20 | Prefix filter + empty fast path; only true candidates hashed |

Peak RSS is the child process maximum resident set from
`resource.getrusage(RUSAGE_CHILDREN)` (bytes on macOS, KiB×1024 on Linux).
Elapsed time is wall clock around that child. Numbers move with CPU load,
filesystem cache, and libc/`libexif` versions — treat them as order-of-magnitude
baselines.

## `photoc duplicates` performance notes

Changes landed to improve large-directory and same-size workloads without
concurrency:

1. **Avoid unnecessary hashing** — Within a size cohort, read a short leading
   prefix (64 bytes). Full SHA-256 runs only when at least two files share that
   prefix. Empty files use the known empty digest with no I/O. Duplicate groups
   and savings are unchanged; `files_hashed` can be lower when unique same-size
   files are filtered out.
2. **Buffered file reads** — SHA-256 reads use a 64 KiB buffer. `open` still
   uses `O_NONBLOCK` so FIFOs cannot hang, then clears it for sequential reads.
3. **Allocation reduction** — Paths collected during the walk live in a chunked
   arena (freed once). Group path strings are copied only for files that enter
   a duplicate group.
4. **In-memory hash for tiny files** — When the prefix read already consumed the
   whole file, SHA-256 runs over that buffer instead of re-opening the path.

At this baseline, concurrency was not added: single-threaded hashing remained the clear bottleneck
only when many files share both size and prefix (true near-duplicate or
duplicate sets). Prefix filtering removes most of that work for typical photo
libraries with unique content. The later bounded-concurrency measurements are below.

### Remaining bottlenecks

1. **True duplicate / shared-prefix cohorts** — Still one full SHA-256 per
   candidate; large identical photos dominate runtime.
2. **Directory walk + `lstat`** — Every regular file is sized before grouping.
3. **Prefix reads on huge same-size cohorts** — Unique files still pay one short
   read each when many files share a size (common in the synthetic 4 KiB
   benchmark corpus).
4. **Path arena + result copies** — Duplicate-heavy trees copy winning paths
   into owned group strings.

## `photoc stats` bottlenecks

1. **Per-JPEG metadata load** — `photo_load_metadata` for every JPEG.
2. **Directory walk** — recursive `photoc_fs_walk_recursive`.
3. **Aggregation / stdout** — minor next to metadata I/O.

## Bounded concurrency

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DPHOTOC_BENCHMARKS=ON
cmake --build build-bench
python3 scripts/benchmark-concurrency.py
```

The script uses temporary synthetic data, warms each workload once per worker
count, and reports the median of three measured runs. It validates matching
result checksums across worker counts. Focus requires `cjpeg` (from
libjpeg-turbo) to generate a 2048×2048 checkerboard JPEG. No user photos are
read or changed. The optional C harness links to the same core library as the
CLI; metadata and duplicates timings include directory traversal and pool
startup, while focus includes traversal, scaled decode, and analysis.

Measured on 2026-09-28, Apple Silicon, macOS 27, Release build, warm cache:

| Workload | Files | 1 worker | 2 workers | 4 workers | Gain with 2 |
| --- | ---: | ---: | ---: | ---: | ---: |
| EXIF scan, mixed tiny JPEG fixtures | 4000 | 0.1524 s | 0.0944 s | 0.0707 s | 1.61× |
| Small EXIF scan (stays serial) | 8 | 0.0004 s | 0.0004 s | 0.0003 s | 1.01× |
| Duplicate scan, shared prefix, 2 MiB each | 64 | 0.2955 s | 0.1513 s | 0.0842 s | 1.95× |
| Duplicate scan, unique prefixes, 4 KiB each | 1000 | 0.0156 s | 0.0150 s | 0.0152 s | 1.04× |
| Sharpness API, generated checkerboards | 128 | 0.6857 s | 0.3546 s | 0.1875 s | 1.93× |

These results justify two workers for substantial full-hash workloads and
batched EXIF loading. They do not establish a benefit for prefix filtering or
directory enumeration, which remain serial. Disk/cache behavior and CPU count
can change the gains; these are synthetic warm-cache measurements, not claims
about every photo library.

- The shared pool defaults to **2** workers, accepts **1** for serial execution,
  and rejects counts above **8**. Core APIs accept a worker count for future
  configuration; there is no new CLI flag.
- Duplicates starts a pool only with at least **4** full-hash candidates totaling
  at least **1 MiB**. Prefix filtering, tiny in-memory hashes, and empty-file
  hashes stay serial. Resource failures fall back to serial work.
- The scanner buffers at most **32** regular entries and parallelizes batches
  containing at least **8** JPEGs once a pool exists. Pool startup requires a
  batch with at least **16** JPEGs; an 8-file scan measured slightly slower
  with thread startup, so it stays serial. Boundary trials measured 1.22× for
  16 JPEGs and 1.81× for four 256 KiB full-hash candidates. Each task owns its
  `Photo` and libexif handles.
  This follows [libexif's thread-safety requirements](https://libexif.github.io/api/).
  Metadata and path memory is bounded by the batch, rather than directory size.
- Tasks write to separate result slots. Warnings, aggregation, and user callbacks
  run on the calling thread in input/traversal order. Output never depends on
  worker completion order. Pools finish outstanding tasks and join all workers
  before resources are freed. Callback stop discards loaded results after that
  callback and retains only preceding scan counts.
- The `focus` core benchmark exercises independent sharpness calls through the
  pool. The `focus` CLI command still walks and analyzes serially; see the
  image-processing baselines below for its current end-to-end cost. Analysis
  buffers scale with active workers, so the conservative default also limits
  memory use.

The full test suite was run with ThreadSanitizer without race reports. System
libexif and TurboJPEG binaries are not themselves rebuilt with sanitizer
instrumentation; the test verifies instrumented project code and observed
library calls, and does not prove all possible executions race-free.

## Image-processing CLI baselines

`scripts/benchmark-image.py` measures the decode/encode-backed commands end
to end through the CLI. It generates a two-template high-frequency color
checkerboard corpus with `cjpeg` (128 photos at 1280x960 for `focus`/`check`,
64 for the heavier `compress`/`contact` workloads), warms each workload once,
and reports the median of three timed runs plus peak child RSS. The `--target`
corpus uses half the source JPEG size so the quality search runs without an
unreachable target (which would make `compress` exit 1).

Recorded on 2026-10-09 with a local Release build (`cmake --build build`, no
sanitizers), Apple Silicon laptop, warm disk cache:

| Command | Files | Elapsed (s) | Files/s | Peak RSS |
| --- | ---: | ---: | ---: | ---: |
| `focus --recursive` | 128 | 0.47 | 270.0 | 23.0 MiB |
| `check --recursive` | 128 | 1.264 | 101.2 | 6.5 MiB |
| `compress --quality 80` | 64 | 1.773 | 36.1 | 13.9 MiB |
| `compress --target 278KiB` | 64 | 2.210 | 29.0 | 18.7 MiB |
| `contact --recursive` | 64 | 0.502 | 127.4 | 17.8 MiB |

A repeat run varied by about 1-4%, so treat smaller differences as noise.
`focus` now scores photos through the shared worker pool (two workers): it
improved from 0.871 s / 147 files/s at 13.0 MiB to about 0.47 s / 270 files/s
at ~23 MiB peak RSS, close to the ~1.9x two-worker scaling
`benchmark-concurrency.py` measures for sharpness. The extra memory is one set
of decode/analysis buffers per worker. `check`, `compress`, and `contact` are
still single-threaded. The corpus is synthetic, so use it for before/after
comparisons rather than as a claim about a specific photo library.

## Progress rendering

`photoc_progress` samples the monotonic clock on TTY runs. The cost is small:
`clock_gettime(CLOCK_MONOTONIC)` measured about 17 ns/call on the Apple
Silicon host above, against roughly 20 µs per file for a warm `stats` scan
(1000 files in 0.021 s), so a per-file clock read is around 0.08% of that
workload. `src/core/progress.c` still limits the reads: after each visible
render it derives a stride from the observed update rate so the clock is
consulted only as often as the 90 ms display cadence needs, capped at eight
calls, with one for sparse updates. Under a PTY this reduced `compress` redraws
for a 64-file corpus from 24 to 4 with the same final summary and wall time
(~0.25 s). The non-TTY benchmark scripts disable progress, so their numbers
are unchanged within noise.

## Interpreting changes

When comparing a future run:

- Prefer the same `--count` and a warm cache (run twice, keep the second).
- For the image baselines, keep `--size`, `--quality`, and `--trials`
  consistent; the target label is derived from the source size at run time.
- Do not mix sanitizer builds with baseline rows.
- A drop in files/s on `stats` usually points at metadata/I/O; on `duplicates`,
  at hashing or path allocation. For duplicates, also check `files hashed` in
  the command summary — a higher hash count means more shared size+prefix work.
- Memory regressions matter more than small timing noise on laptop CPUs.

# Benchmarks

Lightweight wall-clock measurements for `photoc stats` and
`photoc duplicates`. These runs generate synthetic corpora, time the built
CLI, and report files/sec plus peak child RSS. They are for regression
awareness and bottleneck notes — not a formal performance suite.

## Running

```sh
cmake -S . -B build && cmake --build build
python3 scripts/benchmark.py
python3 scripts/benchmark.py --count 5000 --keep
```

| Flag | Meaning |
| --- | --- |
| `--count N` | Approximate files per corpus (default `1000`) |
| `--photoc PATH` | Binary to measure (default `build/photoc`) |
| `--data-dir DIR` | Corpus root (default `benchmarks/data`, gitignored) |
| `--keep` | Leave the generated trees after the run |
| `--json` | Also print a JSON blob after the text report |

The script rebuilds corpora each run:

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

Concurrency was not added: single-threaded hashing remains the clear bottleneck
only when many files share both size and prefix (true near-duplicate or
duplicate sets). Prefix filtering removes most of that work for typical photo
libraries with unique content.

### Remaining bottlenecks

1. **True duplicate / shared-prefix cohorts** — Still one full SHA-256 per
   candidate; large identical photos dominate runtime.
2. **Directory walk + `lstat`** — Every regular file is sized before grouping.
3. **Prefix reads on huge same-size cohorts** — Unique files still pay one short
   read each when many files share a size (common in the synthetic 4 KiB
   benchmark corpus).
4. **Path arena + result copies** — Duplicate-heavy trees copy winning paths
   into owned group strings.

## `photoc stats` bottlenecks (unchanged)

1. **Per-JPEG metadata load** — `photo_load_metadata` for every JPEG.
2. **Directory walk** — recursive `photoc_fs_walk_recursive`.
3. **Aggregation / stdout** — minor next to metadata I/O.

## Interpreting changes

When comparing a future run:

- Prefer the same `--count` and a warm cache (run twice, keep the second).
- Do not mix sanitizer builds with baseline rows.
- A drop in files/s on `stats` usually points at metadata/I/O; on `duplicates`,
  at hashing or path allocation. For duplicates, also check `files hashed` in
  the command summary — a higher hash count means more shared size+prefix work.
- Memory regressions matter more than small timing noise on laptop CPUs.

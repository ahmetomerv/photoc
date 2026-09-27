# Benchmarks

Lightweight wall-clock measurements for `photoc stats` and
`photoc duplicates`. These runs generate synthetic corpora, time the built
CLI, and report files/sec plus peak child RSS. They are for regression
awareness and bottleneck notes — not a formal performance suite.

No application optimizations are implied by these numbers.

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
- **duplicates** — many unique 4 KiB files of the same size (forces SHA-256)
  plus a few exact duplicate groups.

## Baseline results

Recorded on 2026-09-27 with an unoptimized Release-style local build
(`cmake --build build`, no sanitizers):

| Host | Notes |
| --- | --- |
| Darwin 27.0.0 arm64 | Apple Silicon laptop, warm disk cache |

| Command | Files | Elapsed (s) | Files/s | Peak RSS |
| --- | ---: | ---: | ---: | ---: |
| `stats --recursive` | 1000 | 0.049 | 20617 | 6.2 MiB |
| `duplicates --recursive` | 1000 | 0.074 | 13562 | 6.2 MiB |
| `stats --recursive` | 5000 | 0.188 | 26560 | 6.3 MiB |
| `duplicates --recursive` | 5000 | 0.359 | 13924 | 6.9 MiB |

Peak RSS is the child process maximum resident set from
`resource.getrusage(RUSAGE_CHILDREN)` (bytes on macOS, KiB×1024 on Linux).
Elapsed time is wall clock around that child. Numbers move with CPU load,
filesystem cache, and libc/`libexif` versions — treat them as order-of-magnitude
baselines.

## Likely bottlenecks

### `photoc stats`

1. **Per-JPEG metadata load** — `photoc_scan_directory` calls
   `photo_load_metadata` for every JPEG (open, `photoc_jpeg_inspect`, libexif
   parse, string copies into `Photo`). With tiny fixtures this dominates over
   pixel decode.
2. **Directory walk** — recursive `photoc_fs_walk_recursive` visits every
   entry; nested `batch_*` trees add syscall cost.
3. **Aggregation** — `photoc_stats_add_photo` grows count tables; cheap at
   these sizes relative to metadata I/O.
4. **Stdout** — human-readable report formatting is minor next to scanning.

Not stressed here: large JPEG decode, broken-file warning volume, or JSON
output.

### `photoc duplicates`

1. **SHA-256 of same-size groups** — after collecting sizes, every file that
   shares a size is hashed (`photoc_hash_file_sha256`). The corpus intentionally
   uses one shared size so almost every file is hashed.
2. **Path storage** — each visited file keeps an owned path string until
   grouping finishes; RSS grows with file count (visible in the 5k run).
3. **Sort / group assembly** — `qsort` by digest and group appends are
   secondary at these counts.
4. **Walk** — same recursive directory walk as stats.

Not stressed here: very large files (hash throughput), millions of unique
sizes (hash skipped), or deep symlink/permission failure paths.

## Interpreting changes

When comparing a future run:

- Prefer the same `--count` and a warm cache (run twice, keep the second).
- Do not mix sanitizer builds with baseline rows.
- A drop in files/s on `stats` usually points at metadata/I/O; on `duplicates`,
  at hashing or path allocation.
- Memory regressions matter more than small timing noise on laptop CPUs.

# photoc

`photoc` is a command-line toolkit for photographers, written in C. It can
inspect JPEG metadata and summarize JPEG collections. Other planned commands
are placeholders.

## Build and test

Requires a C17 compiler, CMake 3.21 or newer, a `pkg-config` implementation,
and the libexif development package:

```sh
# macOS (Homebrew)
brew install cmake pkgconf libexif

# Debian/Ubuntu
sudo apt install cmake pkg-config libexif-dev

# Fedora
sudo dnf install cmake pkgconf-pkg-config libexif-devel
```

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

You can run the same steps with `sh scripts/build-and-test.sh`.

## CLI

```sh
./build/photoc --help
./build/photoc --version
./build/photoc exif --help
./build/photoc exif photo.jpg
./build/photoc exif photo.jpg --json
./build/photoc stats ~/Pictures --recursive
./build/photoc stats ~/Pictures --json
```

`exif <file>` prints file, image, camera, exposure, date, and location details
for one JPEG. Unavailable EXIF fields are labeled `Unavailable`.

`stats <directory> [--recursive]` reports parsed photo count, total storage,
average file size, earliest and latest valid EXIF capture timestamps, and the
most-used camera model, ISO, aperture, and focal length. Its distributions show
counts and percentages of all successfully parsed photos; missing EXIF values
are omitted, so a distribution can total less than 100%. It scans one directory
level by default. Unreadable or invalid JPEGs produce warnings on stderr while
the scan continues. Counts are sorted by frequency, with deterministic ties.
The planned `compress`, `duplicates`, `rename`, `sort`, `focus`, and `scrub`
commands still report that they are not implemented.

With `exif --json`, details are grouped under `file`, `image`, `camera`,
`exposure`, `date`, and `location`. Missing values are `null`; `has_gps` is a
boolean, and sizes, dimensions, exposure values, and coordinates are numbers.
With `stats --json`, output has `scan`, `storage`, `capture_dates`, and
`distributions` objects. Distributions contain sorted arrays of camera models,
ISO values, apertures, and focal lengths, each with `value`, `count`, and
`percentage_of_photos` fields. Numeric metadata stays numeric, unavailable
dates and averages are `null`, and per-file warnings still go to stderr.

Global options are `-h`/`--help`, `--version` (also `-V`), `-v`/`--verbose`, `-q`/`--quiet`, and `--json`. Options can appear before or after a command. `--` ends option parsing. Verbose and quiet cannot be combined; they do not alter command output yet. JSON output is available for `exif` and `stats`.

Exit status is `0` for success, `1` for file or metadata errors, `2` for usage
errors, and `3` for unimplemented commands. Normal output uses stdout;
diagnostics use stderr.

## Shared filesystem utilities

[`include/photoc/fs.h`](include/photoc/fs.h) defines the filesystem API used by future commands. It covers path inspection, filename and extension extraction, safe path joining, and callback-based directory walks. [`include/photoc/photo.h`](include/photoc/photo.h) defines the shared `Photo` metadata model. Both headers document ownership and unavailable values.

[`include/photoc/json.h`](include/photoc/json.h) provides a small string writer
for JSON output without another dependency.

`photo_load_metadata` loads JPEG dimensions, file size, and available EXIF fields
into a `Photo`. It reports distinct results for unsupported extensions, invalid
JPEG data, filesystem errors, and allocation failures. Missing EXIF fields are
left unavailable. Call `photo_cleanup` after a successful load.

[`include/photoc/scan.h`](include/photoc/scan.h) defines a reusable directory
scanner. It can walk one level or recurse, loads each JPEG into a temporary
`Photo`, and calls the supplied photo callback. A warning callback receives
per-file JPEG failures while scanning continues. Aggregate counts distinguish
regular files visited, JPEGs found, photos parsed, skipped files, and errors.
The scanner owns and frees each `Photo` after its callback returns.

[`include/photoc/stats.h`](include/photoc/stats.h) defines the reusable
aggregation API used by `photoc stats`. It owns frequency labels, ignores
unavailable values, and sorts frequency tables separately from CLI formatting.

[`include/photoc/filename_template.h`](include/photoc/filename_template.h)
defines the filename-template API for future rename operations. It expands
`{date}`, `{datetime}`, `{camera}`, `{make}`, `{iso}`, `{aperture}`, `{focal}`,
`{sequence}`, `{original}`, and `{ext}` using a `Photo`. For example,
`{date}_{camera}_{sequence}.{ext}` can produce
`2026-09-27_EOS_R5_0007.JpEg` with sequence width 4. Dates use the captured
EXIF time (`YYYY-MM-DD` or `YYYY-MM-DD_HH-MM-SS`); missing required metadata
returns an error. The original filename stem and extension retain their
letter case. Unsafe filename characters become underscores; no file is renamed.

## EXIF dependency

[`libexif`](https://libexif.github.io/) is a C library for reading, editing,
and serializing EXIF metadata. CMake finds the system installation through its
`libexif.pc` file; the library is not vendored. It is LGPL-licensed and remains
separate from photoc's MIT-licensed source. Binary distributors must meet the
LGPL requirements for the library they ship or link.

libexif's direct file-loading API targets JPEG. Its save API produces an EXIF
data buffer, so safely writing modified metadata back into an image file will
need additional work. RAW and HEIC support are outside this integration.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). The project is licensed under the [MIT License](LICENSE).

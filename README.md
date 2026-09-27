# photoc

`photoc` is a command-line toolkit for photographers, written in C. It can
inspect JPEG metadata, summarize JPEG collections, find exact duplicate files,
rename JPEGs, sort JPEGs, and create JPEG copies without EXIF GPS tags.
Other planned commands are placeholders.

## Build and test

Requires a C17 compiler, CMake 3.21 or newer, a `pkg-config` implementation,
the libexif development package, and libjpeg-turbo's TurboJPEG development
package:

```sh
# macOS (Homebrew)
brew install cmake pkgconf libexif jpeg-turbo

# Debian/Ubuntu
sudo apt install cmake pkg-config libexif-dev libturbojpeg0-dev

# Fedora
sudo dnf install cmake pkgconf-pkg-config libexif-devel turbojpeg-devel
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
./build/photoc compress photo.jpg --quality 80
./build/photoc compress photo.jpg --target 2MB --min-quality 30
./build/photoc exif --help
./build/photoc exif photo.jpg
./build/photoc exif photo.jpg --json
./build/photoc stats ~/Pictures --recursive
./build/photoc stats ~/Pictures --json
./build/photoc duplicates ~/Pictures --recursive
./build/photoc duplicates ~/Pictures --json
./build/photoc rename ~/Pictures --format "{date}_{camera}_{sequence}.{ext}"
./build/photoc rename ~/Pictures --format "{date}_{camera}_{sequence}.{ext}" --apply
./build/photoc sort ~/Pictures --by date --recursive
./build/photoc sort ~/Pictures --by session --gap 30m
./build/photoc sort ~/Pictures --by date --apply
./build/photoc scrub photo.jpg --gps
./build/photoc scrub ~/Pictures --gps --recursive
./build/photoc scrub photo.jpg --gps --in-place
```

`exif <file>` prints file, image, camera, exposure, date, and location details
for one JPEG. Unavailable EXIF fields are labeled `Unavailable`.

`compress <file|directory> [--quality <1-100> | --target <size> [--min-quality <1-100>]] [--recursive] [--output-dir <directory>]`
re-encodes JPEGs at quality 80 by default. It creates `photo.compressed.jpg`
beside `photo.jpg`, or in the requested output directory. Directory scans are
flat unless `--recursive` is given. A separate output directory preserves the
source's relative subdirectories and is created if needed. Existing outputs
are skipped in directory mode; originals are never changed or replaced.
Directory reports include processed, skipped, and failed counts, input and
output byte totals for successful files, and total savings. Single-file reports
show both sizes, bytes saved, and percentage saved. Savings can be negative if
the new files are larger. JPEG EXIF fields, including GPS, are copied where libexif can
represent them. Other metadata segments such as ICC or XMP are not copied.
Re-encoding is lossy, so keep the original when image quality matters.

Use `--target <size>` instead of `--quality` to search for the highest quality
whose output fits the requested size. Sizes are positive integer bytes, or
`KB`/`MB`/`GB` (decimal) and `KiB`/`MiB`/`GiB` (binary); for example, `2MB`
means 2,000,000 bytes. `--min-quality <1-100>` sets the floor (default 20).
The reported quality and achieved size include copied EXIF. If the target is
unreachable at the minimum quality, photoc writes that best-effort copy,
reports the miss, and exits with status 1. This also works per JPEG in a
directory scan.

`stats <directory> [--recursive]` reports parsed photo count, total storage,
average file size, earliest and latest valid EXIF capture timestamps, and the
most-used camera model, ISO, aperture, and focal length. Its distributions show
counts and percentages of all successfully parsed photos; missing EXIF values
are omitted, so a distribution can total less than 100%. It scans one directory
level by default. Unreadable or invalid JPEGs produce warnings on stderr while
the scan continues. Counts are sorted by frequency, with deterministic ties.

`duplicates <directory> [--recursive]` compares regular files of any type by
size, then hashes only same-size candidates with SHA-256. It reports each exact
duplicate group in deterministic order. `Total duplicate files` includes every
file in those groups; potential savings count all but one file per group. The
savings estimate is based on file sizes and does not account for hard links or
filesystem sharing. The command never deletes or changes files. Symlinks are
ignored; unreadable files produce warnings and a non-zero exit status.
Use `--json` for a structured report with `duplicate_group_count`,
`duplicate_file_count`, `potential_savings_bytes`, and `groups`. Each group
contains `file_size_bytes`, a lowercase `sha256` digest, and `paths`. JSON
paths are escaped, and the command emits no human-readable text in JSON mode.

`rename <directory> --format <template> [--recursive] [--apply]` is a dry run
by default. It prints planned JPEG renames as `old_name -> new_name` and makes
no changes. Add `--apply` to perform them. Recursive output uses paths relative
to the supplied directory. JPEGs are sorted by path before sequence numbers
are assigned, starting at 0001;
invalid JPEGs and entries missing required EXIF fields still consume a number.
The original extension's casing is preserved when using `{ext}`. Existing
destinations and duplicate planned destinations (including ASCII case-only
differences) are blocked. Each blocked JPEG gets a reason on stderr. With
`--apply`, the entire set is checked before any file is changed; a blocked
entry cancels the whole apply. The command uses an exclusive rename operation
so a destination that appears later is never overwritten. If a rename fails
midway, photoc attempts to restore earlier names in reverse order and reports
any restoration failure. Another process creating a former source name can
prevent complete rollback; photoc will leave that file in its new location
rather than overwrite the competing path. The summary reports JPEG, planned,
unchanged, blocked, applied, and rolled-back counts. Exit status is 1 for
blocked or failed operations. A missing date does not fall back to file
timestamps. Use `photoc rename --help` for examples.

`sort <directory> --by date [--recursive] [--apply]` groups JPEGs into
`YYYY/MM/DD/` folders using valid EXIF capture dates. It preserves filenames,
prints paths relative to the supplied directory, and previews changes by
default. It reports missing dates, invalid JPEGs, existing destinations, and
duplicate planned destinations as blocked; a blocked JPEG gives exit status 1.
Existing files or symlinks in the destination date path are also reported.
Use `--recursive` to scan nested directories. The plan is sorted by source
path for predictable output.

`sort <directory> --by session [--gap <duration>] [--recursive] [--apply]` plans
moving JPEGs into `session-001/`, `session-002/`, and so on. Session numbers
follow EXIF capture-time order, with source path breaking timestamp ties.
Consecutive photos remain together when their gap is no greater than the
threshold. The default is 60 minutes; `--gap 30m` and `--gap 2h` set other
thresholds. Missing or invalid dates are blocked. Session plans use the same
collision checks as date plans. `--gap` is accepted only with `--by session`.

Add `--apply` to either sort mode to create destination directories and move
the files. The whole plan is checked first; any blocked entry prevents every
move. Destination directories are opened without following symlinks, and moves
use an exclusive operation that will not overwrite a file created after
preflight. If a later step fails, photoc attempts to move earlier files back
and remove directories it created, reporting any restoration failure. The
apply summary includes planned, unchanged, blocked, applied, and rolled-back
counts. Concurrent filesystem changes can prevent full restoration; photoc
will not overwrite a competing file to complete rollback.

`scrub <file|directory> --gps [--recursive] [--in-place]` removes EXIF GPS tags
from JPEGs. By default it creates copies named like `photo.scrubbed.jpg`; it
never changes the source or replaces an existing destination. Directory scans
include one level by default and can include nested directories with
`--recursive`. JPEGs without GPS tags and non-JPEG directory entries are
skipped. The report lists each file whose GPS
tags were removed and totals processed, skipped, failed, and GPS-removed files.
Invalid or unreadable JPEGs and output collisions count as failures, while
other files continue processing. Scrub does not remove location data from XMP
or MakerNotes.

**Warning:** `--in-place` replaces each original JPEG after writing and
verifying a temporary file in the same directory. It creates no backup.
The replacement keeps POSIX permission bits and group ownership. It refuses
symlinks, hard links, files owned by another user, and sources changed since
metadata was read. ACLs and extended attributes are not copied. If permission
preservation, writing, or verification fails, the original remains in place
and the temporary file is removed. The final
rename is atomic on a local filesystem; as with other portable POSIX tools,
concurrent replacement of the same path cannot be prevented completely.

The planned `focus` command still reports that it is not implemented.

With `exif --json`, details are grouped under `file`, `image`, `camera`,
`exposure`, `date`, and `location`. Missing values are `null`; `has_gps` is a
boolean, and sizes, dimensions, exposure values, and coordinates are numbers.
With `stats --json`, output has `scan`, `storage`, `capture_dates`, and
`distributions` objects. Distributions contain sorted arrays of camera models,
ISO values, apertures, and focal lengths, each with `value`, `count`, and
`percentage_of_photos` fields. Numeric metadata stays numeric, unavailable
dates and averages are `null`, and per-file warnings still go to stderr.

Global options are `-h`/`--help`, `--version` (also `-V`), `-v`/`--verbose`, `-q`/`--quiet`, and `--json`. Options can appear before or after a command. `--` ends option parsing. Verbose and quiet cannot be combined; they do not alter command output yet. JSON output is available for `exif`, `stats`, and `duplicates`.

Exit status is `0` for success, `1` for file or metadata errors, `2` for usage
errors, and `3` for unimplemented commands. Normal output uses stdout;
diagnostics use stderr.

## Shared filesystem utilities

[`include/photoc/fs.h`](include/photoc/fs.h) defines the filesystem API used by future commands. It covers path inspection, filename and extension extraction, safe path joining, and callback-based directory walks. [`include/photoc/photo.h`](include/photoc/photo.h) defines the shared `Photo` metadata model. Both headers document ownership and unavailable values.

[`include/photoc/json.h`](include/photoc/json.h) provides a small string writer
for JSON output without another dependency.

[`include/photoc/hash.h`](include/photoc/hash.h) provides streaming SHA-256
hashing of regular files and lowercase hex formatting. The digest and hex
buffers belong to the caller. Hashing failures return `-1` with `errno` set,
and leave the digest buffer unchanged. The implementation is portable C17 and
adds no external dependency.

[`include/photoc/image.h`](include/photoc/image.h) provides JPEG dimensions,
RGB decoding, and quality-configurable JPEG encoding for `photoc compress`.
Decoded pixels and encoded bytes have separate cleanup helpers; encoding returns
bytes in memory and does not write a file or preserve source EXIF/ICC metadata.
The command uses the shared JPEG writer to attach EXIF to the new file.

[`include/photoc/image_analysis.h`](include/photoc/image_analysis.h) provides
grayscale conversion with optional pixel sampling, a 4-neighbor Laplacian, and
population variance. The grayscale and Laplacian buffers have explicit cleanup
helpers and a 16-million-pixel cap; the variance calculation allocates nothing.
These shared functions do not change JPEG files or enable the planned `focus`
command yet.

[`include/photoc/sharpness.h`](include/photoc/sharpness.h) computes variance of
the grayscale Laplacian for a JPEG. It uses TurboJPEG's scaled decode, with a
recommended analysis limit of 1024 pixels on either side and a cap of
4 million pixels for the decoded RGB buffer. The compressed JPEG is still read into
memory. Higher scores indicate more local edge variation, but texture, noise,
JPEG artifacts, and sharpening can also raise the score. Compare images at the
same analysis limit; the value is not an artistic-quality rating or an absolute
sharp/blurred threshold. The `focus` command remains unimplemented.

The image module uses [libjpeg-turbo's TurboJPEG C API](https://libjpeg-turbo.org/Documentation/Documentation).
CMake finds the system library through `libturbojpeg.pc`; it is not vendored.

[`include/photoc/duplicates.h`](include/photoc/duplicates.h) defines the
read-only duplicate-finding API. The result owns its groups and paths; callers
release them with `photoc_duplicates_cleanup`.

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

[`include/photoc/session.h`](include/photoc/session.h) groups photos already
sorted by EXIF capture time. Consecutive photos stay in one session when their
gap is at most the configured threshold (60 minutes by default). It writes
stable, 1-based IDs to a caller-owned array; missing timestamps receive ID 0
and break the session chain. Invalid or out-of-order timestamps return an error
without changing the output. The sort command uses it for session plans.

[`include/photoc/filename_template.h`](include/photoc/filename_template.h)
defines the filename-template API for future rename operations. It expands
`{date}`, `{datetime}`, `{camera}`, `{make}`, `{iso}`, `{aperture}`, `{focal}`,
`{sequence}`, `{original}`, and `{ext}` using a `Photo`. For example,
`{date}_{camera}_{sequence}.{ext}` can produce
`2026-09-27_EOS_R5_0007.JpEg` with sequence width 4. Dates use the captured
EXIF time (`YYYY-MM-DD` or `YYYY-MM-DD_HH-MM-SS`); missing required metadata
returns an error. The original filename stem and extension retain their
letter case. Unsafe filename characters become underscores. The rename command
uses this API for both previews and applied changes.

## JPEG metadata writing API

[`include/photoc/jpeg_write.h`](include/photoc/jpeg_write.h) exposes reusable
functions to load an editable EXIF copy, deep-copy it, inspect and remove its
GPS IFD, and write a new JPEG. `photoc scrub` uses this API. By default the
source stays untouched; the destination must not exist. The writer creates a
mode-0600 temporary file
beside the destination, flushes and syncs it, reopens and validates the JPEG and
EXIF, compares all bytes outside the replaced EXIF segment with the source,
then uses a no-overwrite rename. It copies compressed image data verbatim, with
no decoding or recompression. Failure paths attempt to remove the temporary
file.

`photoc_jpeg_write_encoded` publishes a JPEG buffer through a verified
temporary file and optionally inserts an EXIF copy. It never replaces an
existing destination.

For explicit in-place scrubbing, `photoc_jpeg_replace_with_exif` uses the same
temporary-file verification, restores the original mode bits and group, checks
the source identity again, then atomically replaces it. It refuses sources with multiple
hard links, symlinks, changed timestamps or identity, or different ownership.

The writer preserves other JPEG segments byte-for-byte and retains non-GPS
EXIF fields that libexif can represent. It rejects ambiguous files with
multiple EXIF APP1 segments or EXIF after the first image scan. libexif has
limited support for some MakerNotes, so unusual maker-specific EXIF may change
when serialized. GPS stored outside the EXIF GPS IFD, such as in XMP or a
MakerNote, is outside this API's scope. Copy mode retains the original for
inspection or recovery.

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

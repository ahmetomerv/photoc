# photoc query

[Command overview](../README.md#commands) · [Output verbosity](../README.md#output-verbosity)

Search JPEG metadata without changing photographs. By default, stdout contains
only matching paths, one per line, sorted in bytewise path order. No filters
means every successfully parsed JPEG matches, including JPEGs without EXIF.

## Usage

```sh
photoc query photo.jpg --iso ">800"
photoc query ~/Photos --camera "DSC-RX100M7A"
photoc query ~/Photos --make "SONY" --aperture "<=4" --focal ">=50"
photoc query ~/Photos --after 2026-01-01 --before 2026-12-31
photoc query ~/Photos --recursive --has-gps
photoc query ~/Photos --recursive --no-gps
```

Supply exactly one regular `.jpg`/`.jpeg` file or directory. Extensions are
case-insensitive. Sony ARW is intentionally not enabled for query in this version. Scans are flat unless `--recursive` is supplied, skip other
files, and do not follow symlinks. A directly supplied symlink or unsupported
extension is refused. `--recursive` requires a directory.

Options may appear before or after the command. Values must be separate
arguments (`--iso ">800"`, not `--iso=">800"`). Use `--` before an input path
beginning with a dash. Each valued filter may be supplied only once.

## Filters

Every supplied filter must match (**AND**). A missing or invalid queried field
does not match; missing ISO is never treated as zero.

| Option | Meaning |
| --- | --- |
| `--camera <value>` | Exact, case-sensitive EXIF camera model. |
| `--make <value>` | Exact, case-sensitive EXIF manufacturer. |
| `--iso <expression>` | Compare ISO. |
| `--aperture <expression>` | Compare aperture f-number. |
| `--focal <expression>` | Compare focal length in millimeters, not a 35 mm equivalent. |
| `--after YYYY-MM-DD` | Capture date on or after the named day, including midnight. |
| `--before YYYY-MM-DD` | Capture date on or before the named day, including 23:59:59. |
| `--has-gps` | Require a valid latitude/longitude pair. |
| `--no-gps` | Require no valid coordinate pair, including missing or invalid GPS tags. |

Use `photoc exif photo.jpg` to see the make/model strings as loaded. The shared
metadata loader removes trailing padding spaces from EXIF strings; query does
not otherwise normalize, fold case, perform substring matching, or expand
wildcards. Empty string filters are invalid.

Numeric expressions accept `100`, `=100`, `>100`, `>=100`, `<100`, and `<=100`.
Numbers must be finite and nonnegative. Decimal fractions and scientific
notation follow the existing numeric parser, for example `<=2.8` and `>=1e3`.
No whitespace is allowed within an expression. Equality compares the loaded
numeric value directly; EXIF rationals are represented as doubles. Quote
comparators to prevent the shell from treating `<` and `>` as redirections.
Malformed expressions, NaN/infinity, negatives, and overflow/underflow are
usage errors.

Dates must be real Gregorian dates from year 0001 through 9999. Bounds are
**inclusive**, and use EXIF `DateTimeOriginal` without timezone conversion or
filesystem-time fallback. Missing or malformed capture timestamps cannot match
a date filter. An inverted date range is invalid. `--has-gps` and `--no-gps`
are mutually exclusive.

## Paths and shell composition

| Option | Output |
| --- | --- |
| Default | Raw paths separated by newlines. |
| `--print0` | Raw paths separated by NUL bytes, including the final path. |
| `--json` | One object containing the query, matches, metadata, and scan summary. |

`--json` and `--print0` are mutually exclusive. Directory results retain the
input directory prefix: querying `./Photos` returns `./Photos/photo.jpg`, so
paths work from the caller's current directory. Absolute inputs produce
absolute paths. A single-file path is preserved as supplied.

Line output is intended for simple inspection; filenames containing newlines
produce multiple apparent lines. Use `--print0` to safely process arbitrary
filenames, including spaces, newlines, and leading dashes:

```sh
# Inspect metadata for every matching path (macOS and Linux).
photoc query ./Photos --recursive --iso ">800" --print0 |
  xargs -0 sh -c 'for path do photoc exif -- "$path" || exit; done' sh

# Extract path and ISO columns from JSON (requires jq).
photoc query ./Photos --recursive --iso ">800" --json |
  jq -r '.matches[] | [.path, .metadata.iso] | @tsv'

# Summarize the search.
photoc query ./Photos --recursive --json | jq '.summary'
```

The `xargs` example passes paths as separate shell arguments; the loop also
handles an empty match list. It stops its batch if an EXIF inspection fails.

A shell pipeline normally returns the last program's status. In shells that
support it, `set -o pipefail` helps detect an incomplete query in a pipeline.
Review downstream commands before using them to modify files.

Quiet keeps every matching path/JSON record. Verbose adds input, output mode,
recursion state, scanner worker limit, visited/discovered/parsed/skipped counts,
metadata failures, and match counts on stderr. Path modes never print progress
or a summary on stdout. Quiet and verbose together return usage status 2.

## JSON contract

```json
{
  "query": {
    "path": "./Photos",
    "recursive": false,
    "filters": {
      "camera": null,
      "make": null,
      "iso": ">800",
      "aperture": null,
      "focal": null,
      "after": null,
      "before": null,
      "gps": null
    },
    "string_matching": "exact_case_sensitive",
    "date_bounds": "inclusive"
  },
  "matches": [
    {
      "path": "./Photos/photo.jpg",
      "metadata": {
        "camera": "DSC-RX100M7A",
        "make": "SONY",
        "iso": 1600,
        "aperture": 4,
        "focal_length_mm": 50,
        "capture_timestamp": "2026:01:01 12:00:00",
        "has_gps": false,
        "latitude": null,
        "longitude": null,
        "width": 5472,
        "height": 3648
      }
    }
  ],
  "summary": {
    "files_visited": 1,
    "jpeg_files_found": 1,
    "photos_parsed": 1,
    "skipped_files": 0,
    "errors": 0,
    "matched": 1
  }
}
```

Filters retain the supplied strings; unspecified filters are `null`. `gps` is
`"present"`, `"absent"`, or `null`. Missing metadata fields are `null`; `has_gps`
is boolean. Coordinates, when available, are decimal degrees. All match paths
use the same ordering and prefix rules as path modes. Valid UTF-8 is preserved;
invalid byte sequences use the shared JSON helper's U+FFFD replacement. Use
`--print0` when exact filesystem bytes are required.

Summary counts describe all scanned regular files, excluding directories and
symlinks. `skipped_files` includes non-JPEG files and JPEG load failures;
`errors` counts JPEG load failures. `matched` counts only successful loads
satisfying every filter. JSON stdout remains JSON only at every verbosity level.

## Exit codes and limits

- **0**: scan completed without JPEG load failures, even with no matches.
- **1**: input, traversal, allocation, output, or JPEG metadata load failure.
- **2**: invalid usage, filter expression/date, or conflicting options.

Directory scans continue after individual JPEG load failures, report each on
stderr even in quiet mode, and return 1 with the successful matches. JSON then
includes partial scan results and error counts. Input/traversal/allocation
failures leave stdout empty. A failed single-file load also leaves stdout empty.

Query reuses the metadata scanner with at most two workers and 32 buffered
regular entries. Only matching metadata records are retained before sorting;
no pixel decoding or image buffers are added. Memory grows with the number of
matches. This is a metadata search, not an integrity audit: use `photoc check`
for full JPEG decoding. Malformed EXIF tags may become unavailable under the
existing metadata loader rather than causing a load failure. The command
cannot find metadata that the shared loader does not expose. It never writes
photographs or sidecars; the OS may update access times while reading.

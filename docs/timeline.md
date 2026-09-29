# photoc timeline

[Command overview](../README.md#commands) · [Output verbosity](../README.md#output-verbosity)

## Purpose

Summarize a photography outing chronologically, grouped by calendar date and
shooting session. Each session reports its first/last capture time, duration,
photo count, total file size, most-used physical focal length and aperture,
ISO range, and camera models when available.

The command reads JPEG and Sony ARW metadata without modifying files or
retaining decoded pixels. It is available in current source builds, not
releases through v0.2.0. See [ARW support](raw.md) for metadata limitations.

## Syntax

```text
photoc timeline <directory> [--recursive] [--gap <duration>] [--json]
photoc timeline --help
```

Exactly one directory is required. Regular `.jpg`, `.jpeg`, and `.arw` files
are recognized case-insensitively. Scans do not follow symlinks.

## Options

| Option | Meaning |
| --- | --- |
| `--recursive` | Include nested directories. Default: scan only the supplied directory. |
| `--gap <duration>` | Maximum gap between consecutive photos within a session. Default: **60 minutes**, shared with `sort --by session`. |
| `--json` | Emit a date/session hierarchy and scan summary. |
| `-q`, `--quiet` | Keep sessions and all result counts, including skipped timestamps and metadata errors; hide the introductory status text and non-critical warnings. |
| `-v`, `--verbose` | Add mode, recursion, gap, worker limit, scan, timestamp, and session diagnostics on stderr. |
| `-h`, `--help` | Show command help. |

Gap values are nonnegative integer minutes (`30m`) or hours (`2h`), using
lowercase suffixes. `0m` groups only equal timestamps. Bare numbers, fractions,
seconds, negative values, uppercase suffixes, and values exceeding
4,294,967,295 minutes are rejected. Give option values as separate arguments.
Quiet and verbose conflict and return usage status **2**.

## Examples

```sh
photoc timeline ~/Pictures/Prague
photoc timeline "Summer trip" --recursive --gap 30m
photoc timeline ./photos --gap 2h --json > timeline.json
photoc timeline --recursive -- -outing

# Optional jq: inspect completeness or select each session with its date.
photoc timeline ./photos --json | jq '.summary'
photoc timeline ./photos --json |
  jq '.dates[] | .date as $date | .sessions[] | {date: $date, start, end, photo_count}'
```

An illustrative session in the human report:

```text
2026-08-23

09:12:00 - 09:47:00
  34 photos
  Duration: 2100 seconds
  Total file size: 102000000 bytes
  Most-used focal length: 9 mm
  Most-used aperture: f/5.6
  ISO range: 100 - 400
  Camera models:
    DSC-RX100M7A: 34
```

A final summary reports included photos, dates, sessions, missing/invalid
capture timestamps, metadata-load errors, and unsupported files. These counts
remain visible in quiet mode. Optional session fields without values are
omitted from the human report and represented as `null` or an empty array in JSON.

## Grouping and statistics

1. Use valid EXIF `DateTimeOriginal` values in `YYYY:MM:DD HH:MM:SS` form.
   Missing and invalid timestamps are counted separately and excluded. There
   is no filesystem-date fallback and no assignment to a guessed session.
2. Sort all dated photos chronologically, regardless of filenames or folders.
3. Group by the recorded calendar date. **Midnight always starts a new group
   and session**, even if adjacent photos are one second apart.
4. Within each date, start a session when the gap from the previous photo
   **exceeds** the threshold. An exact-threshold gap stays in the session.
   Session duration can exceed the threshold because the comparison is
   between consecutive photos, not with the first photo.

The gap comparison reuses the same core rule as `sort --by session` and stats.
Those commands allow sessions across midnight; timeline splits them to provide
its date-first overview. Missing timestamps do not break the dated-photo chain.

Duration is last capture time minus first capture time, in whole seconds. A
single-photo session has duration zero. Counts are per path; duplicates and
hard links each contribute. Total file size sums known logical sizes in bytes,
not disk allocation or deduplicated storage. `photos_with_file_size` identifies
how many included photos contributed a known size.

Most-used focal length and aperture use the shared stats buckets without
standard-stop binning. Ties select the smaller numeric value. Focal length is
**physical millimeters**, never an inferred 35mm equivalent. ISO minimum and
maximum include only known positive values. Camera models group by exact model
string, ordered by descending photo count with case-sensitive lexical ties.
Missing fields do not contribute to that statistic.

### Timezone and clock limitations

Times are the recorded EXIF clock values. The metadata model does not apply
timezone offsets, daylight-saving corrections, or clock synchronization;
timeline does not infer the photographer's timezone from the computer or GPS.
JSON `time_basis` is `recorded_local_exif`. Dates and times have no offset or `Z`.
Duration measures the difference between recorded clock values, not verified
real elapsed time. Cameras with different clock settings can appear in the same
session or in an unexpected order. A session is a time-based grouping, not proof
that photos belong to one event or photographer.

## JSON contract

Successful JSON has these top-level fields in every verbosity mode:

| Field | Type / meaning |
| --- | --- |
| `directory` | String: supplied input path. |
| `recursive` | Boolean. |
| `gap_minutes` | Nonnegative integer. |
| `time_basis` | String: `recorded_local_exif`. |
| `summary` | Object of nonnegative integer counters, described below. |
| `dates` | Chronologically ordered array of `{date, sessions}` objects; empty when no dated photos are available. |

`summary` includes `files_visited` (regular files examined), `photos_parsed`
(successful JPEG/ARW metadata loads), `photos_included` (valid timestamps),
`photos_skipped_timestamp` (missing plus invalid), `missing_timestamp`,
`invalid_timestamp`, `metadata_errors` (failed metadata loads),
`unsupported_files` (unsupported regular files), `dates`, and `sessions`.
Symlinks and non-regular entries are excluded from these counters.
`photos_included + photos_skipped_timestamp = photos_parsed`.

Each date is `YYYY-MM-DD`. Its sessions are chronological and contain:

| Session field | Type / units |
| --- | --- |
| `start`, `end` | Strings: `HH:MM:SS` within the enclosing date. |
| `duration_seconds` | Nonnegative integer. |
| `photo_count` | Positive integer. |
| `total_file_size_bytes` | Nonnegative integer. |
| `photos_with_file_size` | Nonnegative integer. |
| `most_used_focal_length_mm` | Positive number or `null`; physical millimeters. |
| `most_used_aperture` | Positive number or `null`; f-number. |
| `iso_min`, `iso_max` | Positive integers or `null`. |
| `camera_models` | Array of `{model: string, photo_count: positive integer}`; empty when unavailable. |

There is no JSON error envelope. Diagnostics and errors go to stderr; stdout
contains only JSON in JSON mode. Fatal scan/aggregation failures prevent the
report, while output errors can leave a partial document. Consumers should
check the exit status and allow additional fields in future versions.

## Safety and exit statuses

Files are read only. The operating system may update access times. No image
integrity audit is performed; readable metadata does not prove pixel data is
undamaged. Use [check](check.md) to audit JPEG readability.

| Status | Meaning |
| --- | --- |
| `0` | Report completed, including empty scans and missing/invalid timestamps. Per-file metadata warnings alone do not fail the command; inspect `metadata_errors` for completeness. |
| `1` | Fatal directory traversal, allocation, aggregation, or output failure. |
| `2` | Invalid arguments, gap value, or unsupported/conflicting options. |

Broken or unreadable metadata is warned about on stderr, skipped, and counted
separately from missing/invalid timestamps on successfully loaded photos. Keep
files unchanged during the scan for a consistent report. Other file formats
are skipped rather than assigned to sessions.

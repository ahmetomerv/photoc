# photoc stats

[Command overview](../README.md#commands) · [Output verbosity](../README.md#output-verbosity)

## Purpose

Summarize JPEG/ARW photos in a directory: photo count, storage, capture-date range,
camera/lens usage, exposure settings, dimensions, calendar activity, and
shooting sessions. The command
uses the shared metadata scanner and does not modify files.

Sony `.arw` files are supported for common TIFF/EXIF metadata, with the same
command safety/exit behavior. Other RAW formats are unsupported. This is
metadata-only support: see [ARW fields and limits](raw.md). It is available in
current source builds, not releases through v0.2.0.

## Syntax

```text
photoc stats <directory> [--recursive] [--json]
photoc stats --help
```

Exactly one directory is required. Metadata recognition uses `.jpg`, `.jpeg`, and `.arw`,
case-insensitive; other file formats do not contribute to photo statistics.

## Options

| Option | Meaning |
| --- | --- |
| `--recursive` | Include nested directories; the default scans only the supplied directory. |
| `--json` | Emit scan, storage, date-range, and distribution objects. |
| `-h`, `--help` | Print command help and exit successfully. |
| `-v`, `--verbose` | Report mode, recursion, scan/skip/metadata-failure counts, and worker limit on stderr. |
| `-q`, `--quiet` | Keep statistics and distributions; hide scan status and non-critical metadata warnings. JSON keeps scan counters. |

Global verbosity flags work before or after the command. Combining quiet and
verbose returns usage status **2**. JSON, where supported, has the same schema
in every mode; all diagnostics and errors use stderr.

## Examples

```sh
photoc stats ./photos
photoc stats "Photo archive" --recursive
photoc stats ./photos --recursive --json > stats.json
photoc stats --recursive -- -archive

# Optional jq: inspect scan completeness and storage.
photoc stats ./photos --json | jq '{scan, storage, sessions}'
photoc stats ./photos --json | jq '.distributions.months'
photoc stats ./photos --json | jq '.distributions.shutter_speeds_seconds'
```

## Output and interpretation

The terminal report includes total parsed photos, total storage in bytes,
average/median file size, earliest/latest capture times, most-used values, scan
counters, and distributions.

Distribution rows are ordered by descending frequency. Camera-model ties use
case-sensitive lexical order; numeric ties use ascending numeric value. The
first row determines the reported most-used value, even in a tie.

Percentages use **all successfully parsed photos** as the denominator, not
only photos with that field. If two of four photos have a known ISO, the ISO
percentages sum to 50%. Unavailable fields are omitted, never counted as zero.
Numeric buckets use their formatted metadata values; there is no rounding into
standard aperture or focal-length bins. Camera models are grouped by model
string, not by make/model combination.

Average size is total bytes divided by photos with a known file size. Capture
dates include only valid `DateTimeOriginal` values in
`YYYY:MM:DD HH:MM:SS` form. No timezone conversion or filesystem-date fallback
is applied.

## Added statistics and definitions

The original human report and four distribution sections remain unchanged;
new sections are appended. JSON keeps every existing field name, meaning,
and type, and adds fields below. `--by` is not added: the command emits a
single complete report; JSON consumers can select views with `jq`. Clients
should allow additional fields when consuming future reports.

| Statistic | Definition / JSON location |
| --- | --- |
| Shutter speeds | Positive, finite EXIF ExposureTime in **seconds**, grouped with the same `%.15g` numeric precision as existing exposure buckets. No rounding to standard stops. `distributions.shutter_speeds_seconds`. |
| Orientation | Landscape, portrait, or square, from both positive stored dimensions. EXIF orientations 5–8 swap width/height for classification. Absent/invalid orientation uses stored dimensions. `distributions.orientations`. |
| Resolution / megapixels | Exact **stored** `width x height` buckets, regardless of orientation. Each `distributions.resolutions` row includes `width`, `height`, and `megapixels = width * height / 1,000,000`. Distinct resolutions remain distinct even at the same pixel count; no megapixel binning. |
| Year | Full calendar year `YYYY`, using valid capture timestamps. `distributions.years`. |
| Month | Full calendar month `YYYY-MM`, kept separate across years. `distributions.months`. |
| Day | Full date `YYYY-MM-DD`. `distributions.days`. |
| Hour | Recorded local hour **0–23**, pooled across dates, without timezone or daylight-saving conversion. `distributions.hours`. |
| Lens | Standard EXIF **LensModel** only, grouped by exact string after existing trailing-space trimming. No MakerNote parsing, LensSpecification inference, or camera/lens lookup. `distributions.lens_models`. |
| 35mm-equivalent focal length | Only a positive standard EXIF **FocalLengthIn35mmFilm** SHORT value. `distributions.focal_lengths_35mm_equivalent_mm`, explicitly labeled “35mm equivalent.” Physical focal length stays in the existing `focal_lengths_mm`; no crop factor is guessed. |
| Median file size | Middle known size for odd counts; arithmetic mean of the two middle known sizes for even counts. `storage.median_file_size_bytes`; null with no known sizes. Zero bytes is a known size. Average size retains its original definition. |

Lens and equivalent focal values are loaded by the shared JPEG/ARW metadata
backends for these statistics; this does not add fields to the `exif` command's
existing output or change query/rename template behavior. A physical **9mm**
focal length is still reported as **9mm**, even if an explicit equivalent
value is **24mm**. Without that tag the equivalent statistic is unavailable.

All distributions retain descending-frequency ordering and the denominator
of all parsed photos. Text/calendar ties use case-sensitive bytewise order;
numeric ties use ascending numeric value. Resolution ties use ascending pixel
count, then the resolution label. Human percentages use one decimal place;
JSON uses the existing 15-significant-digit numeric format. Raw counts are
integers; independently rounded human percentages need not sum exactly to 100%.

`unavailable` contains a count for each distribution key, plus `file_size`.
It includes missing and invalid values. In each distribution, available row
counts plus its unavailable count equal the parsed-photo total. Empty tables
are arrays in JSON and `None` in text. Human added distributions also show
unavailable counts. Calendar statistics all require a complete, valid timestamp;
partial dates and impossible dates are omitted.

### Shooting sessions

`sessions` summarizes **only photos with valid capture timestamps**, sorted
chronologically. It reuses the existing session-gap rule with the fixed default
**60 minutes**: a gap **greater than** 60 minutes starts another session; an
exactly 60-minute gap stays in the same session. Duplicate timestamps stay
together. Missing/invalid timestamps are excluded before grouping; their unknown
chronological position does not split the dated-photo chain.

The object contains `gap_minutes`, `dated_photos`, `unavailable_photos`, session
`count`, `average_photos_per_session`, `smallest_session`, and `largest_session`.
The average is **dated photos / session count**, not all parsed photos divided
by sessions. Smallest/largest are photo counts. With zero dated photos there
are zero sessions and null average/minimum/maximum. Sessions may cross midnight,
month, or year boundaries. Camera clocks are taken as recorded; a session is a
time-based grouping across the collection, not proof of one photographer/event.

For a per-date chronological session overview with a configurable gap, use
[timeline](timeline.md). Its date-first grouping always splits at midnight.

Stats retains only owned bucket strings and scalar file sizes/capture seconds
for median/session sorting (O(photos) scalar storage), never decoded pixel data.
File-size samples sort in O(n log n). ARW dimensions are stored metadata values,
which can include sensor margins as described in [ARW support](raw.md).

## Edge cases

- A JPEG without EXIF still counts as a photo and contributes its file size.
  Its dimensions still contribute orientation/resolution rows; missing EXIF
  contributes no camera/exposure/calendar rows or session.
- An empty directory succeeds: zero photos/storage, unavailable average and
  dates, and empty distributions (`None` in terminal output).
- Broken or unreadable JPEG/ARW photos are warned about, skipped, and counted as errors.
  **Per-file warnings alone do not cause a non-zero exit status.** Inspect the
  normal scan summary or JSON `scan.errors` when completeness matters. Quiet
  mode suppresses these warnings and the human scan summary, but JSON counters
  remain available.
- Fatal directory traversal, allocation, aggregation, or output errors fail
  the command. Directory scans do not follow symlinks; symlinks and other
  non-regular entries are excluded from scan counters.
- Invalid calendar timestamps are ignored for the date range. Photos from
  cameras with different clock settings are compared as recorded local times.
- Counts are per path, not unique content; duplicate or hard-linked JPEG/ARW photos each
  contribute. Use [duplicates](duplicates.md) to find exact copies.

### Scan counters

| Field | Meaning |
| --- | --- |
| `files_visited` | Regular files examined, including unsupported files; excludes directories. |
| `arw_files_found` | Regular `.arw` files, including files that fail to load. |
| `metadata_files_found` | JPEG + ARW candidates. |
| `jpeg_files_found` | Regular `.jpg`/`.jpeg` files, including files that fail to load. |
| `photos_parsed` | JPEG/ARW photos successfully loaded; the total photo count. |
| `skipped_files` | Unsupported regular files plus JPEG/ARW photos that failed to load. |
| `errors` | JPEG/ARW metadata load failures, a subset of skipped files. |

## Safety notes

The command is read only. Storage reports describe logical JPEG/ARW file sizes,
not filesystem allocation, total directory disk usage, or deduplicated space.
Files should remain unchanged while scanning for a consistent report.

Normal reports and JSON go to stdout; warnings/errors go to stderr. There is no
JSON error envelope. Loading/aggregation failure prevents the normal report;
an output failure can leave partial JSON.

## JSON schema

All fields below are present. Empty distributions are arrays, unavailable
dates/average/median/session summaries are `null`, and exposure values are numbers. The JSON
photo total is `scan.photos_parsed`; there is no separate `total_photos` key.
Most-used values are the first distribution entries, when present; JSON does
not add separate most-used fields. Date strings retain EXIF format.

Schema for the current successful report:

```json
{
  "title": "photoc stats output",
  "type": "object",
  "required": ["scan", "storage", "capture_dates", "distributions", "unavailable", "sessions"],
  "additionalProperties": false,
  "properties": {
    "scan": {
      "type": "object",
      "required": [
        "directory",
        "recursive",
        "files_visited",
        "jpeg_files_found",
        "arw_files_found",
        "metadata_files_found",
        "photos_parsed",
        "skipped_files",
        "errors"
      ],
      "additionalProperties": false,
      "properties": {
        "directory": {"type": "string"},
        "recursive": {"type": "boolean"},
        "files_visited": {"$ref": "#/$defs/count"},
        "jpeg_files_found": {"$ref": "#/$defs/count"},
        "arw_files_found": {"$ref": "#/$defs/count"},
        "metadata_files_found": {"$ref": "#/$defs/count"},
        "photos_parsed": {"$ref": "#/$defs/count"},
        "skipped_files": {"$ref": "#/$defs/count"},
        "errors": {"$ref": "#/$defs/count"}
      }
    },
    "storage": {
      "type": "object",
      "required": ["total_bytes", "photos_with_file_size", "average_file_size_bytes", "median_file_size_bytes"],
      "additionalProperties": false,
      "properties": {
        "total_bytes": {"$ref": "#/$defs/count"},
        "photos_with_file_size": {"$ref": "#/$defs/count"},
        "average_file_size_bytes": {"type": ["number", "null"], "minimum": 0},
        "median_file_size_bytes": {"type": ["number", "null"], "minimum": 0}
      }
    },
    "capture_dates": {
      "type": "object",
      "required": ["earliest", "latest"],
      "additionalProperties": false,
      "properties": {"earliest": {"type": ["string", "null"]}, "latest": {"type": ["string", "null"]}}
    },
    "distributions": {
      "type": "object",
      "required": [
        "camera_models",
        "iso",
        "apertures",
        "focal_lengths_mm",
        "lens_models",
        "focal_lengths_35mm_equivalent_mm",
        "shutter_speeds_seconds",
        "orientations",
        "resolutions",
        "years",
        "months",
        "days",
        "hours"
      ],
      "additionalProperties": false,
      "properties": {
        "camera_models": {"type": "array", "items": {"$ref": "#/$defs/cameraBucket"}},
        "iso": {"type": "array", "items": {"$ref": "#/$defs/isoBucket"}},
        "apertures": {"type": "array", "items": {"$ref": "#/$defs/numericBucket"}},
        "focal_lengths_mm": {"type": "array", "items": {"$ref": "#/$defs/numericBucket"}},
        "lens_models": {"type": "array", "items": {"$ref": "#/$defs/cameraBucket"}},
        "focal_lengths_35mm_equivalent_mm": {"type": "array", "items": {"$ref": "#/$defs/isoBucket"}},
        "shutter_speeds_seconds": {"type": "array", "items": {"$ref": "#/$defs/numericBucket"}},
        "orientations": {"type": "array", "items": {"$ref": "#/$defs/orientationBucket"}},
        "resolutions": {"type": "array", "items": {"$ref": "#/$defs/resolutionBucket"}},
        "years": {"type": "array", "items": {"$ref": "#/$defs/yearBucket"}},
        "months": {"type": "array", "items": {"$ref": "#/$defs/monthBucket"}},
        "days": {"type": "array", "items": {"$ref": "#/$defs/dayBucket"}},
        "hours": {"type": "array", "items": {"$ref": "#/$defs/hourBucket"}}
      }
    },
    "unavailable": {
      "type": "object",
      "required": [
        "camera_models",
        "iso",
        "apertures",
        "focal_lengths_mm",
        "lens_models",
        "focal_lengths_35mm_equivalent_mm",
        "shutter_speeds_seconds",
        "orientations",
        "resolutions",
        "years",
        "months",
        "days",
        "hours",
        "file_size"
      ],
      "additionalProperties": false,
      "properties": {
        "camera_models": {"$ref": "#/$defs/count"},
        "iso": {"$ref": "#/$defs/count"},
        "apertures": {"$ref": "#/$defs/count"},
        "focal_lengths_mm": {"$ref": "#/$defs/count"},
        "lens_models": {"$ref": "#/$defs/count"},
        "focal_lengths_35mm_equivalent_mm": {"$ref": "#/$defs/count"},
        "shutter_speeds_seconds": {"$ref": "#/$defs/count"},
        "orientations": {"$ref": "#/$defs/count"},
        "resolutions": {"$ref": "#/$defs/count"},
        "years": {"$ref": "#/$defs/count"},
        "months": {"$ref": "#/$defs/count"},
        "days": {"$ref": "#/$defs/count"},
        "hours": {"$ref": "#/$defs/count"},
        "file_size": {"$ref": "#/$defs/count"}
      }
    },
    "sessions": {
      "type": "object",
      "required": [
        "gap_minutes",
        "dated_photos",
        "unavailable_photos",
        "count",
        "average_photos_per_session",
        "smallest_session",
        "largest_session"
      ],
      "additionalProperties": false,
      "properties": {
        "gap_minutes": {"const": 60},
        "dated_photos": {"$ref": "#/$defs/count"},
        "unavailable_photos": {"$ref": "#/$defs/count"},
        "count": {"$ref": "#/$defs/count"},
        "average_photos_per_session": {"type": ["number", "null"], "minimum": 1},
        "smallest_session": {"type": ["integer", "null"], "minimum": 1},
        "largest_session": {"type": ["integer", "null"], "minimum": 1}
      }
    }
  },
  "$defs": {
    "count": {"type": "integer", "minimum": 0},
    "percentage": {"type": "number", "minimum": 0, "maximum": 100},
    "cameraBucket": {
      "type": "object",
      "required": ["value", "count", "percentage_of_photos"],
      "additionalProperties": false,
      "properties": {
        "value": {"type": "string"},
        "count": {"type": "integer", "minimum": 1},
        "percentage_of_photos": {"$ref": "#/$defs/percentage"}
      }
    },
    "isoBucket": {
      "type": "object",
      "required": ["value", "count", "percentage_of_photos"],
      "additionalProperties": false,
      "properties": {
        "value": {"type": "integer", "minimum": 1},
        "count": {"type": "integer", "minimum": 1},
        "percentage_of_photos": {"$ref": "#/$defs/percentage"}
      }
    },
    "numericBucket": {
      "type": "object",
      "required": ["value", "count", "percentage_of_photos"],
      "additionalProperties": false,
      "properties": {
        "value": {"type": "number", "exclusiveMinimum": 0},
        "count": {"type": "integer", "minimum": 1},
        "percentage_of_photos": {"$ref": "#/$defs/percentage"}
      }
    },
    "orientationBucket": {
      "type": "object",
      "required": ["value", "count", "percentage_of_photos"],
      "additionalProperties": false,
      "properties": {
        "value": {"enum": ["landscape", "portrait", "square"]},
        "count": {"type": "integer", "minimum": 1},
        "percentage_of_photos": {"$ref": "#/$defs/percentage"}
      }
    },
    "yearBucket": {
      "type": "object",
      "required": ["value", "count", "percentage_of_photos"],
      "additionalProperties": false,
      "properties": {
        "value": {"type": "string", "pattern": "^[0-9]{4}$"},
        "count": {"type": "integer", "minimum": 1},
        "percentage_of_photos": {"$ref": "#/$defs/percentage"}
      }
    },
    "monthBucket": {
      "type": "object",
      "required": ["value", "count", "percentage_of_photos"],
      "additionalProperties": false,
      "properties": {
        "value": {"type": "string", "pattern": "^[0-9]{4}-[0-9]{2}$"},
        "count": {"type": "integer", "minimum": 1},
        "percentage_of_photos": {"$ref": "#/$defs/percentage"}
      }
    },
    "dayBucket": {
      "type": "object",
      "required": ["value", "count", "percentage_of_photos"],
      "additionalProperties": false,
      "properties": {
        "value": {"type": "string", "pattern": "^[0-9]{4}-[0-9]{2}-[0-9]{2}$"},
        "count": {"type": "integer", "minimum": 1},
        "percentage_of_photos": {"$ref": "#/$defs/percentage"}
      }
    },
    "hourBucket": {
      "type": "object",
      "required": ["value", "count", "percentage_of_photos"],
      "additionalProperties": false,
      "properties": {
        "value": {"type": "integer", "minimum": 0, "maximum": 23},
        "count": {"type": "integer", "minimum": 1},
        "percentage_of_photos": {"$ref": "#/$defs/percentage"}
      }
    },
    "resolutionBucket": {
      "type": "object",
      "required": ["value", "count", "percentage_of_photos", "width", "height", "megapixels"],
      "additionalProperties": false,
      "properties": {
        "value": {"type": "string", "pattern": "^[0-9]+x[0-9]+$"},
        "count": {"type": "integer", "minimum": 1},
        "percentage_of_photos": {"$ref": "#/$defs/percentage"},
        "width": {"type": "integer", "minimum": 1, "maximum": 4294967295},
        "height": {"type": "integer", "minimum": 1, "maximum": 4294967295},
        "megapixels": {"type": "number", "exclusiveMinimum": 0}
      }
    }
  }
}
```

## Exit statuses

| Status | Meaning |
| --- | --- |
| `0` | Report completed, possibly with per-file warnings recorded in `scan.errors`. |
| `1` | Fatal scanning, aggregation, allocation, or output failure. |
| `2` | Invalid argument count or unsupported/conflicting CLI options. |

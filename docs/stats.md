# photoc stats

[Command overview](../README.md#commands)

## Purpose

Summarize JPEG photos in a directory: photo count, storage, capture-date range,
and camera model, ISO, aperture, and focal-length distributions. The command
uses the shared metadata scanner and does not modify files.

## Syntax

```text
photoc stats <directory> [--recursive] [--json]
photoc stats --help
```

Exactly one directory is required. JPEG recognition uses `.jpg` and `.jpeg`,
case-insensitive; other file formats do not contribute to photo statistics.

## Options

| Option | Meaning |
| --- | --- |
| `--recursive` | Include nested directories; the default scans only the supplied directory. |
| `--json` | Emit scan, storage, date-range, and distribution objects. |
| `-h`, `--help` | Print command help and exit successfully. |
| `-v`, `--verbose` | Recognized; currently does not change output. |
| `-q`, `--quiet` | Recognized; currently does not change output. Cannot be combined with verbose. |

## Examples

```sh
photoc stats ./photos
photoc stats "Photo archive" --recursive
photoc stats ./photos --recursive --json > stats.json
photoc stats --recursive -- -archive

# Optional jq: inspect scan completeness and storage.
photoc stats ./photos --json | jq '{scan, storage}'
```

## Output and interpretation

The terminal report includes total parsed photos, total storage in bytes,
average file size, earliest/latest capture times, most-used values, scan
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

## Edge cases

- A JPEG without EXIF still counts as a photo and contributes its file size.
  Its missing metadata contributes no distribution entries or capture date.
- An empty directory succeeds: zero photos/storage, unavailable average and
  dates, and empty distributions (`None` in terminal output).
- Broken or unreadable JPEGs are warned about, skipped, and counted as errors.
  **Per-file warnings alone do not cause a non-zero exit status.** Inspect the
  scan summary or JSON `scan.errors` when completeness matters.
- Fatal directory traversal, allocation, aggregation, or output errors fail
  the command. Directory scans do not follow symlinks; symlinks and other
  non-regular entries are excluded from scan counters.
- Invalid calendar timestamps are ignored for the date range. Photos from
  cameras with different clock settings are compared as recorded local times.
- Counts are per path, not unique content; duplicate or hard-linked JPEGs each
  contribute. Use [duplicates](duplicates.md) to find exact copies.

### Scan counters

| Field | Meaning |
| --- | --- |
| `files_visited` | Regular files examined, including non-JPEG files; excludes directories. |
| `jpeg_files_found` | Regular `.jpg`/`.jpeg` files, including files that fail to load. |
| `photos_parsed` | JPEGs successfully loaded; the total photo count. |
| `skipped_files` | Non-JPEG regular files plus JPEGs that failed to load. |
| `errors` | JPEG load failures, a subset of skipped files. |

## Safety notes

The command is read only. Storage reports describe logical JPEG file sizes,
not filesystem allocation, total directory disk usage, or deduplicated space.
Files should remain unchanged while scanning for a consistent report.

Normal reports and JSON go to stdout; warnings/errors go to stderr. There is no
JSON error envelope. Loading/aggregation failure prevents the normal report;
an output failure can leave partial JSON.

## JSON schema

All fields below are present. Empty distributions are arrays, unavailable
dates/average are `null`, and ISO/aperture/focal values are numbers. The JSON
photo total is `scan.photos_parsed`; there is no separate `total_photos` key.
Most-used values are the first distribution entries, when present; JSON does
not add separate most-used fields. Date strings retain EXIF format.

Schema for the current successful report:

```json
{
  "title": "photoc stats output",
  "type": "object",
  "required": ["scan", "storage", "capture_dates", "distributions"],
  "additionalProperties": false,
  "properties": {
    "scan": {
      "type": "object",
      "required": ["directory", "recursive", "files_visited", "jpeg_files_found", "photos_parsed", "skipped_files", "errors"],
      "additionalProperties": false,
      "properties": {
        "directory": {"type": "string"},
        "recursive": {"type": "boolean"},
        "files_visited": {"$ref": "#/$defs/count"},
        "jpeg_files_found": {"$ref": "#/$defs/count"},
        "photos_parsed": {"$ref": "#/$defs/count"},
        "skipped_files": {"$ref": "#/$defs/count"},
        "errors": {"$ref": "#/$defs/count"}
      }
    },
    "storage": {
      "type": "object",
      "required": ["total_bytes", "photos_with_file_size", "average_file_size_bytes"],
      "additionalProperties": false,
      "properties": {
        "total_bytes": {"$ref": "#/$defs/count"},
        "photos_with_file_size": {"$ref": "#/$defs/count"},
        "average_file_size_bytes": {"type": ["number", "null"], "minimum": 0}
      }
    },
    "capture_dates": {
      "type": "object",
      "required": ["earliest", "latest"],
      "additionalProperties": false,
      "properties": {
        "earliest": {"type": ["string", "null"]},
        "latest": {"type": ["string", "null"]}
      }
    },
    "distributions": {
      "type": "object",
      "required": ["camera_models", "iso", "apertures", "focal_lengths_mm"],
      "additionalProperties": false,
      "properties": {
        "camera_models": {"type": "array", "items": {"$ref": "#/$defs/cameraBucket"}},
        "iso": {"type": "array", "items": {"$ref": "#/$defs/isoBucket"}},
        "apertures": {"type": "array", "items": {"$ref": "#/$defs/numericBucket"}},
        "focal_lengths_mm": {"type": "array", "items": {"$ref": "#/$defs/numericBucket"}}
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

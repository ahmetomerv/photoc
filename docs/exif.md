# photoc exif

[Command overview](../README.md#commands) · [Output verbosity](../README.md#output-verbosity)

## Purpose

Inspect selected metadata from one JPEG or Sony ARW without modifying it. The report
contains File, Image, Camera, Exposure, Date, and Location sections. This is
a focused summary, not a dump of every EXIF tag.

Sony `.arw` files are supported for common TIFF/EXIF metadata, with the same
command safety/exit behavior. Other RAW formats are unsupported. This is
metadata-only support: see [ARW fields and limits](raw.md). It is available in
current source builds, not releases through v0.2.0.

## Syntax

```text
photoc exif <file> [--json]
photoc exif --help
```

Exactly one regular JPEG or Sony ARW file is required. `.jpg`, `.jpeg`, and `.arw` extensions are
recognized case-insensitively. Directory and recursive operation are not
supported; use [stats](stats.md) to summarize a collection.

## Options

| Option | Meaning |
| --- | --- |
| `--json` | Emit one JSON object containing the same metadata as the terminal report. |
| `-h`, `--help` | Print command help and exit successfully. |
| `-v`, `--verbose` | Report input path, metadata mode, output format, and metadata parse failures on stderr. |
| `-q`, `--quiet` | Keep all EXIF fields; errors remain visible. |

Global verbosity flags work before or after the command. Combining quiet and
verbose returns usage status **2**. JSON, where supported, has the same schema
in every mode; all diagnostics and errors use stderr.

## Examples

```sh
photoc exif photo.jpg
photoc exif "Summer trip/IMG_001.JPG"
photoc exif photo.jpg --json > metadata.json
photoc exif -- -photo.jpg

# jq is optional and installed separately.
photoc exif photo.jpg --json | jq '.location'
```

The path in output is the supplied path, not an automatically resolved
absolute path. Dimensions are stored width and height, without applying
EXIF orientation. ARW dimensions may include sensor margins; preview dimensions
are never substituted.

## Edge cases

- Missing EXIF is valid. JPEG dimensions and file size remain available; absent
  fields display as `Unavailable`, or `null` in JSON.
- Capture time comes from `DateTimeOriginal`, normally
  `YYYY:MM:DD HH:MM:SS`. It is returned as stored; this command does not validate
  its calendar value, infer a date from filesystem timestamps, or interpret a
  timezone. [Sort](sort.md) and [rename](rename.md) validate dates before use.
- Numeric metadata that cannot be read as a supported, positive value is
  unavailable. Exposure time is in seconds and focal length is in millimeters.
- `has_gps` is true only when both latitude and longitude can be decoded with
  valid references and ranges. Coordinates use signed decimal degrees: south
  and west are negative. A false value does **not** prove that every location
  tag has been removed.
- Invalid/truncated JPEG structure or malformed ARW metadata, unreadable files, and unsupported
  extensions produce errors. Renaming a PNG to `.jpg` does not make it valid.
  JPEG inspection is not a full pixel decode or a complete EXIF integrity check.
- A symlink supplied as the file is not accepted as a regular file.

## Safety notes

The command is read only. Output can expose GPS coordinates, camera identity,
capture time, and local paths; review it before publishing. Use
[scrub](scrub.md) to remove EXIF GPS tags from a copy.

Normal reports and JSON go to stdout; diagnostics go to stderr. On a loading
failure there is no metadata object on stdout. Check exit status before using
redirected JSON; an output failure can leave a partial document.

Width/height may be unavailable in ARW and are then null in JSON. Orientation
is the standard EXIF/TIFF value 1–8, or null; photoc does not rotate pixels.
`file.format` identifies `jpeg` or `sony_arw`.

## JSON schema

All fields shown below are present on successful output. Optional metadata is
`null`, presence is a boolean, and numbers remain numeric. `date.captured` is
an EXIF string, not an ISO 8601 timestamp. No JSON error envelope is emitted.

Schema for the current successful output:

```json
{
  "title": "photoc exif output",
  "type": "object",
  "required": ["file", "image", "camera", "exposure", "date", "location"],
  "additionalProperties": false,
  "properties": {
    "file": {
      "type": "object",
      "required": ["name", "path", "size_bytes", "format"],
      "additionalProperties": false,
      "properties": {
        "name": {"type": "string"},
        "path": {"type": "string"},
        "size_bytes": {"type": "integer", "minimum": 0},
        "format": {"enum": ["jpeg", "sony_arw"]}
      }
    },
    "image": {
      "type": "object",
      "required": ["width", "height", "orientation"],
      "additionalProperties": false,
      "properties": {
        "width": {"type": ["integer", "null"], "minimum": 1},
        "height": {"type": ["integer", "null"], "minimum": 1},
        "orientation": {"type": ["integer", "null"], "minimum": 1, "maximum": 8}
      }
    },
    "camera": {
      "type": "object",
      "required": ["make", "model"],
      "additionalProperties": false,
      "properties": {
        "make": {"type": ["string", "null"]},
        "model": {"type": ["string", "null"]}
      }
    },
    "exposure": {
      "type": "object",
      "required": ["iso", "aperture", "exposure_time_seconds", "focal_length_mm"],
      "additionalProperties": false,
      "properties": {
        "iso": {"type": ["integer", "null"], "minimum": 1},
        "aperture": {"type": ["number", "null"], "exclusiveMinimum": 0},
        "exposure_time_seconds": {"type": ["number", "null"], "exclusiveMinimum": 0},
        "focal_length_mm": {"type": ["number", "null"], "exclusiveMinimum": 0}
      }
    },
    "date": {
      "type": "object",
      "required": ["captured"],
      "additionalProperties": false,
      "properties": {"captured": {"type": ["string", "null"]}}
    },
    "location": {
      "type": "object",
      "required": ["has_gps", "latitude", "longitude"],
      "additionalProperties": false,
      "properties": {
        "has_gps": {"type": "boolean"},
        "latitude": {"type": ["number", "null"], "minimum": -90, "maximum": 90},
        "longitude": {"type": ["number", "null"], "minimum": -180, "maximum": 180}
      }
    }
  }
}
```

When `has_gps` is false, both coordinate fields are `null`; when true, both
are numbers. Human-readable coordinates are rounded to six decimal places;
JSON retains more precision.

## Exit statuses

| Status | Meaning |
| --- | --- |
| `0` | Metadata report written, including JPEG/ARW photos with missing EXIF. |
| `1` | Unsupported file format, invalid JPEG/ARW metadata, I/O, allocation, or output failure. |
| `2` | Invalid argument count or unsupported/conflicting CLI options. |

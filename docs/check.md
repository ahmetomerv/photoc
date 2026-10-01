# photoc check

[Command overview](../README.md#commands) · [Output verbosity](scripting.md#output-verbosity)

## Purpose

Audit a JPEG or a collection for structural readability. The command reads
marker structure, decodes every image scanline, and inspects EXIF for invalid
headers, directory/value offsets, and libexif corruption diagnostics. It never
repairs, recompresses, renames, or deletes photographs or writes sidecars.

## Syntax and examples

```sh
photoc check photo.jpg
photoc check ~/Pictures --recursive
photoc check ~/Pictures --recursive --only-errors
photoc check ~/Pictures --json
photoc check ~/Pictures --recursive --json > check.json
photoc --verbose check ~/Pictures --recursive
photoc check -- -photo.jpg
```

Exactly one regular `.jpg`/`.jpeg` file or directory is required; extension
matching is case-insensitive. Directory scans are flat unless `--recursive`
is supplied. Non-JPEG files and symbolic links are skipped, and links are not
followed. Directly supplied symlinks and unsupported extensions are refused.
`--recursive` with a single file returns usage status 2. Existing compressed
or scrubbed copies are checked like any other JPEG.

| Option | Behavior |
| --- | --- |
| `--recursive` | Include nested directories. |
| `--only-errors` | Show ERROR rows only, excluding OK and WARNING rows. Summary counts still include every checked JPEG. Applies to human and JSON output. |
| `--json` | Write one JSON object to stdout. Errors and verbose diagnostics go to stderr. |
| `-q`, `--quiet` | Keep audit rows and summary counts; suppress the explanatory footer. Warnings in rows are requested audit results. Failure explanations remain on stderr. |
| `-v`, `--verbose` | Add mode, input path, recursion/filter state, serial worker count, discovered/checked/skipped counts, and warning/error counts on stderr. |
| `-h`, `--help` | Print help. |

Options may appear before or after the command. Quiet and verbose together
return usage status 2. There is no modifying mode, `--apply`, or repair option.

## Statuses and exit codes

| Status | Meaning |
| --- | --- |
| OK | Marker structure parsed and full pixel decoding completed without a detected issue. Missing EXIF is valid. |
| WARNING | Pixels decoded, but EXIF is malformed/unreadable, the marker layout is unusual, or the decoder reported a recoverable issue. |
| ERROR | Pixels could not be decoded, or an I/O error, unsupported codec feature, changed file, or resource limit prevented a reliable complete check. |

Exit **0** means no ERROR entries; warnings are acceptable for automation but
should still be reviewed. Exit **1** means at least one ERROR or an operational
failure. Exit **2** means invalid usage. `--only-errors` and verbosity never
change these rules.

Some truncated JPEGs still decode because the library reconstructs missing
data or an end marker. These are WARNING, with wording that explicitly says
the decoder recovered and the image may be incomplete. Fatal truncation is
ERROR. A successful decode is not proof that every original pixel survived.

## Human output

Rows are sorted by path using bytewise ordering, independent of directory
enumeration order. Directory row paths are relative to the input directory;
a single-file path is reported as supplied. Control bytes and backslashes in
human row paths are escaped so filenames cannot create fake report rows.

```text
OK       DSC0001.JPG
WARNING  DSC0002.JPG  malformed or unreadable EXIF; pixels still decode
ERROR    DSC0003.JPG  truncated JPEG data prevented decoding

Files checked: 3
OK: 1
Warnings: 1
Errors: 1
```

Rows and summary counts go to stdout. Each ERROR also has an unconditional
failure explanation on stderr, including in quiet and JSON modes. Empty
directories finish successfully with zero counts. A traversal failure aborts
before checking the collected paths and produces stderr only. An individual
JPEG failure does not stop other files from being checked.

## JSON contract

```json
{
  "summary": {
    "files_checked": 1,
    "ok": 1,
    "warnings": 0,
    "errors": 0,
    "files_skipped": 0
  },
  "files": [
    {
      "path": "photo.jpg",
      "status": "ok",
      "code": "readable",
      "message": "JPEG structure and pixels are readable"
    }
  ]
}
```

`status` is `ok`, `warning`, or `error`. `code` is a stable reason identifier;
use it rather than matching message text. One primary finding is reported per
file: errors take priority, followed by pixel/structure warnings and then EXIF
warnings. Multiple problems are not exhaustively listed. `files_skipped`
counts regular non-JPEG files; directories and symlinks are excluded. Summary
counts cover all checked files even when `files` is filtered by `--only-errors`.

| Code | Meaning |
| --- | --- |
| `readable` | No detected issue. |
| `empty_file` | Zero-byte file. |
| `invalid_signature` | Missing JPEG SOI signature. |
| `invalid_structure` | Marker structure is invalid and decoding failed. |
| `truncated_jpeg` | Truncation detected; status distinguishes decoder recovery from fatal failure. |
| `decode_failed` | Fatal decoder failure despite readable marker structure. |
| `jpeg_warning` | Recoverable decoder warning. |
| `structure_warning` | Strict marker parsing failed, but pixels still decoded. |
| `malformed_exif` | EXIF is invalid, unreadable, or outside supported directory bounds. Pixels still decoded. |
| `unusual_exif` | Duplicate EXIF markers or EXIF after an image scan. Pixels still decoded. |
| `io_error` | File could not be read. |
| `resource_limit` | Audit could not finish within limits, or decoder memory was unavailable. |
| `unsupported_jpeg` | JPEG process/precision or conversion unsupported by the linked decoder. |
| `file_changed` | File size or modification/change timestamps changed during reading. |
| `internal_error` | Audit setup or allocation failure. |

JSON stdout stays JSON only in every verbosity mode. Invalid usage, unsupported
input paths, and traversal failures occur before a report and leave stdout
empty. Valid UTF-8 paths are retained; invalid byte sequences use the existing
JSON helper's U+FFFD replacement.

## Safety, performance, and limits

The shared JPEG parser reads bounded marker payloads. The libjpeg interface of
the existing libjpeg-turbo dependency streams compressed data and decodes into
one row buffer; the command never retains whole file/image buffers. Progressive
and other multi-scan images require decoder coefficient storage. Checks run
serially to keep that memory from multiplying across workers. Only paths and
small findings are retained for deterministic reports.

Files are limited to **512 MiB**, images to **100 million pixels**, scans to
**256**, and estimated padded multi-scan coefficient storage to **256 MiB**.
EXIF directory graphs are bounded to 64 directories, and aggregate value/entry
storage is limited to 4 MiB before loading. Reaching a decode/resource
limit gives an ERROR described as an incomplete check, not confirmed corruption.
JPEG support depends on the installed library; tests cover 8-bit baseline and
progressive RGB/CMYK, grayscale, and existing EXIF fixtures.

This is not a visual-quality, authenticity, backup, or comprehensive metadata
validator. It does not verify ICC/XMP contents, MakerNote internals, thumbnails,
or every EXIF tag's semantic value. Data after the first image's EOI is outside
the audit. Unsupported processes may be perfectly valid JPEGs. Ordinary file
changes are detected where possible; this is not an atomic filesystem snapshot.
The OS may update access times as a consequence of reading.

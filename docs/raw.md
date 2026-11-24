# Sony ARW support

[Command overview](../README.md#commands)

photoc reads common metadata and the camera's embedded JPEG preview from Sony
`.arw` files. It never decodes, develops, or recompresses RAW sensor data, and
never modifies an ARW file: `scrub` writes a separate copy.

## Supported commands

| Command | JPEG | Sony ARW | Other RAW formats |
| --- | --- | --- | --- |
| `exif` | Metadata | Common TIFF/EXIF metadata | Unsupported |
| `stats` | Metadata statistics | Metadata statistics | Skipped |
| `timeline` | Capture-date/session overview | Capture-date/session overview | Skipped |
| `rename` | Preview/apply | Preview/apply, same safety checks | Skipped |
| `sort` | Date/session preview/apply | Date/session preview/apply, same safety checks | Skipped |
| `query` | Metadata search | Metadata search | Skipped |
| `check` | JPEG structure and full pixel decode | Container, sensor data layout, and full preview decode | Skipped |
| `contact` | JPEG contact sheets | Thumbnails from the embedded preview | Skipped |
| `focus` | JPEG sharpness | Embedded preview at the full image's analysis size | Skipped |
| `review` | Interactive marking | Interactive marking; embedded preview shown | Skipped |
| `scrub` | GPS, privacy, or descriptive metadata removal | `--gps` and `--privacy` copies (`.scrubbed.ARW`) | Skipped |
| `compress` | JPEG recompression | Skipped | Skipped |
| `duplicates` | Exact file bytes | Exact file bytes | Exact file bytes |

Extensions `.jpg`, `.jpeg`, and `.arw` are matched case-insensitively.
`compress` keeps explicit JPEG discovery and reports skipped ARW files.
Directory scans skip unsupported extensions and symlinks. Unsupported
single-file inputs fail. `review` accepts a directory only and keeps its marks
in a separate JSON state file; it never changes JPEG or ARW bytes. See
[Review](review.md).

```sh
photoc exif DSC00001.ARW
photoc exif DSC00001.ARW --json
photoc stats ./photos --recursive --json
photoc query ./photos --camera "ILCE-7M2" --iso ">=800"
photoc check ./photos --recursive
photoc scrub DSC00001.ARW --privacy
photoc rename ./photos --format '{date}_{camera}_{sequence}.{ext}'
photoc sort ./photos --by date
```

`rename` and `sort` preview by default. Use `--apply` only after reviewing the
plan. They retain the existing whole-plan preflight, collision detection,
no-overwrite operations, changed-source checks, and best-effort rollback.
Sorting keeps the filename/extension; renaming's `{ext}` preserves the source
extension including its case. A literal extension in a user template is still
literal and does not convert file contents. Invalid ARW metadata blocks an
entry just as invalid JPEG metadata does. Sorting requires a valid capture
stamp; no filesystem-time fallback is added.

## Metadata fields

| Photo field | ARW source and behavior |
| --- | --- |
| File size/format | Opened regular file size; `sony_arw` format selected by extension and checked as classic TIFF. |
| Width/height | Standard EXIF PixelXDimension/PixelYDimension when present; otherwise the largest non-reduced CFA/linear-RAW TIFF image directory with both dimensions. These are stored tag dimensions, potentially including sensor margins, and are not rotated. Preview dimensions are never substituted. |
| Make/model | IFD0 Make/Model ASCII tags, with existing trailing-space trimming. |
| Capture timestamp | EXIF DateTimeOriginal, preserving the existing text representation without timezone conversion. |
| ISO | Standard ISO Speed Ratings SHORT/LONG value. |
| Aperture | EXIF FNumber rational (f-number). |
| Exposure time | EXIF ExposureTime rational (seconds). |
| Focal length | EXIF FocalLength rational (millimeters). |
| Stats lens usage | Standard EXIF LensModel ASCII only. |
| Stats 35mm-equivalent focal length | Positive EXIF FocalLengthIn35mmFilm SHORT, kept separate from physical focal length; no crop-factor guess. |
| GPS | Standard latitude/longitude rationals and reference tags; both coordinates must be valid. |
| Orientation | Standard IFD0 orientation 1–8; no pixels are rotated or decoded. |

Missing or invalid field values remain unavailable. The existing Photo presence
flags distinguish absent dimensions/numbers from zero. EXIF JSON adds
`file.format` (`jpeg`/`sony_arw`) and `image.orientation` (number or null);
width/height may be null for ARW. Human reports say unavailable for absent
ARW dimensions and orientation. JPEG metadata also exposes orientation when
present. No dimensions or exposure values are inferred from the camera model.

Stats includes ARW in photo/storage/date/exposure totals and adds
`scan.arw_files_found` and `scan.metadata_files_found` to JSON. The existing
`scan.jpeg_files_found` continues counting only JPEG candidates. Candidate
counts include load failures; `photos_parsed` includes both successful types.
JPEG-only human summaries retain their wording; mixed summaries name both file
types. Rename/sort summaries use “photos” when ARW candidates are present.
Verbosity never changes JSON fields or the underlying operations.

## Embedded previews

Sony cameras store a JPEG preview in every ARW, typically 1616 x 1080, plus a
160 x 120 thumbnail. `review`, `contact`, `focus`, and `check` read the
largest embedded JPEG (TIFF JPEGInterchangeFormat/Length) outside the CFA/RAW
directories. A missing preview reports "ARW has no usable embedded JPEG
preview". A claimed preview that is out of range or not a JPEG SOI is treated
as an invalid ARW. Previews larger than 64 MiB are refused. Size caps in
`review` and `contact` apply to the preview, never to the whole ARW. Preview
pixels are stored unrotated, so the ARW's EXIF orientation is applied exactly
as for JPEGs.

The preview reflects in-camera processing (picture style, sharpening, lens
corrections) at reduced resolution. `focus` therefore scores it at the size the
full-resolution image would be analyzed at: the recorded EXIF/RAW dimensions
determine the target, and the preview is area-averaged to it, so ARW and JPEG
scores of the same shot are comparable. A preview smaller than that size is
scored as is. On sample A7 II and RX100 VII files, ARW scores came within
roughly 8–18% of a full-resolution JPEG of the same shot, with the same
ordering. Scores remain a relative review aid.

## Structural check

`check` validates an ARW without decoding sensor data:

- the classic-TIFF container and metadata (Sony make, not DNG);
- the CFA sensor directory: every strip or tile must lie within the file, and
  the total size must fit the dimensions and compression (uncompressed 16-bit
  words or packed samples, Sony compressed one byte per pixel, lossless tiles
  starting with a JPEG SOI);
- every embedded JPEG range, and a full decode of the preview with the same
  audit used for JPEG files;
- that the file did not change during the check.

Truncated copies, missing sensor data, and damaged previews are errors. A
missing preview, a recoverable preview warning, or an unrecognized sensor
layout is a warning. An OK result is not proof that every sensor value is
intact. See [check](check.md) for codes.

## Scrubbing ARW copies

`scrub --gps` and `scrub --privacy` write `DSC00001.scrubbed.ARW` beside the
original; the original is never changed and `--in-place` and `--all-metadata`
are refused for ARW. Every edit is size-preserving so no sensor data, preview,
or MakerNote offset moves:

- `--gps` drops the GPS directory pointer and zeroes the GPS directory and its
  values, so no coordinates remain in the copy.
- `--privacy` also removes Artist, CameraOwnerName, BodySerialNumber,
  LensSerialNumber, and ImageUniqueID from every directory (Sony repeats some
  of them in the thumbnail directory) and zeroes their values. Recognized
  private standard XMP properties are removed and the packet is padded back to
  its original size; a packet that cannot shrink back is refused.

Directory tables are rewritten in place with the removed entries dropped and
the freed bytes zeroed. Zeroing that would overlap a kept entry, directory,
preview, or sensor strip is refused. The copy is verified byte for byte
against the source plus the planned edits and re-parsed before it is
published without overwriting. Sony MakerNotes and private data are opaque,
so privacy mode always warns that identifiers may remain there; metadata
inside embedded preview JPEGs is not edited and also triggers the warning.

## Backend and bounds

The generic `Photo` loader dispatches by a shared file-format abstraction.
The ARW backend uses a small, read-only classic-TIFF directory reader, then
reuses libexif's field/value conversions. It never passes the whole ARW or
Sony MakerNotes to an EXIF parser and never invokes a RAW codec.
No additional runtime dependency or third-party implementation is added.

The existing libexif file loader is not a drop-in TIFF/ARW container reader;
its [loading implementation](https://github.com/libexif/libexif/blob/libexif-0_6_26-release/libexif/exif-data.c)
works with EXIF payloads/JPEG extraction. libtiff's
[raster directory reader](https://libtiff.gitlab.io/libtiff/functions/TIFFReadDirectory.html)
and LibRaw's [RAW identification API](https://www.libraw.org/node/33) were
considered. For this limited set of standard metadata tags, a bounded directory
walk avoids importing image/codec requirements or a broader RAW parser.
Vendor metadata, encrypted blocks, and sensor decoding remain outside the backend.

The reader checks file-relative ranges before every read/allocation, including
declared ranges of unknown tags. It uses 64-bit length arithmetic, accepts
little- and big-endian classic TIFF (magic 42), and follows only standard
next-IFD, SubIFD, EXIF, GPS, and interoperability links. It rejects repeated
IFD pointers/cycles, tags repeated within one directory, and malformed
referenced ranges. Tags may appear in any order: TIFF specifies ascending
order, but Sony RAW SubIFDs store some tags out of order. Unknown payloads are
range-checked and skipped without reading them.

Limits: classic TIFF files up to **UINT32_MAX bytes**, at most **64 directories**
and **16,384 total entries**, **2 MiB aggregate metadata reads**, **64 KiB per
selected value**, and **256 KiB retained selected values**. No allocation uses
image dimensions. Resource-limit failures are reported as unsupported limits,
not proof of sensor corruption. BigTIFF, DNG, non-Sony makes, and other RAW
extensions are unsupported. If Make is missing, the `.arw` extension selects
the backend without claiming the manufacturer was verified.

## Known limitations and verification

Only standard common TIFF/EXIF/GPS tags are interpreted. Proprietary/encrypted
Sony MakerNotes, lens corrections, Sony-specific ISO/date overrides,
RAW pixel payloads, XMP sidecars, and ICC contents are not inspected.
Metadata commands report readable metadata even when sensor data is damaged or
missing; use `check` for the structural audit. This support is not a sensor
decode or a promise that every Sony generation exposes these fields and
previews in the same layout.

Tests use original generated containers in
[`tests/fixtures/arw`](../tests/fixtures/arw/README.md), with no camera
photograph. Embedded previews are copies of the project's own JPEG fixtures
and sensor strips are arithmetic gradients. They cover both byte orders,
common fields and missing/invalid values, out-of-order tags, raw/preview
dimension selection, preview selection and malformed preview ranges, every
truncated prefix, malformed lengths/offsets/cycles, mixed collections, sensor
layout checks, scrub edits and refusals, `compress` skipping, preflight, and
ARW rollback. The regular sanitizer suite includes these tests.

A private camera capture can optionally be tested without committing it:

```sh
PHOTOC_ARW_REAL_FIXTURE=/absolute/path/DSC00001.ARW \
  ctest --test-dir build -R arw_cli --output-on-failure
```

Without that environment variable, the real-camera integration case is skipped.
Generated tests establish supported tag handling, not a camera-model compatibility
matrix. Files are read only; the OS may update access times.

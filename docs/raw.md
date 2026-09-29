# Sony ARW metadata support

[Command overview](../README.md#commands)

photoc reads common metadata from Sony `.arw` files without developing,
decoding, recompressing, or rewriting RAW image data. Support is available in
current source builds; releases through **v0.2.0** do not include it.

## Supported commands

| Command | JPEG | Sony ARW | Other RAW formats |
| --- | --- | --- | --- |
| `exif` | Metadata | Common TIFF/EXIF metadata | Unsupported |
| `stats` | Metadata statistics | Metadata statistics | Skipped |
| `timeline` | Capture-date/session overview | Capture-date/session overview | Skipped |
| `rename` | Preview/apply | Preview/apply, same safety checks | Skipped |
| `sort` | Date/session preview/apply | Date/session preview/apply, same safety checks | Skipped |
| `query` | Metadata search | Unsupported/skipped | Unsupported/skipped |
| `check` | JPEG structure and full pixel decode | Unsupported/skipped | Unsupported/skipped |
| `compress` | JPEG recompression | Unsupported/skipped | Unsupported/skipped |
| `focus` | JPEG sharpness | Unsupported/skipped | Unsupported/skipped |
| `scrub` | GPS, privacy, or descriptive metadata removal | Unsupported/skipped | Unsupported/skipped |
| `duplicates` | Exact file bytes | Exact file bytes | Exact file bytes |

Extensions `.jpg`, `.jpeg`, and `.arw` are matched case-insensitively for
metadata-capable discovery. JPEG-only commands keep explicit JPEG discovery;
sharing the metadata scanner does not enable other formats. Directory scans
skip unsupported extensions and symlinks. Unsupported single-file inputs fail.

```sh
photoc exif DSC00001.ARW
photoc exif DSC00001.ARW --json
photoc stats ./photos --recursive --json
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

## Backend and bounds

The generic `Photo` loader dispatches by a shared file-format abstraction.
The ARW backend uses a small, read-only classic-TIFF directory reader, then
reuses libexif's field/value conversions. It never passes the whole ARW or
Sony MakerNotes to an EXIF parser and never invokes a RAW/image codec.
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
IFD pointers/cycles, duplicate or unsorted tags, and malformed referenced
ranges. Unknown payloads are range-checked and skipped without reading them.

Limits: classic TIFF files up to **UINT32_MAX bytes**, at most **64 directories**
and **16,384 total entries**, **2 MiB aggregate metadata reads**, **64 KiB per
selected value**, and **256 KiB retained selected values**. No allocation uses
image dimensions. Resource-limit failures are reported as unsupported limits,
not proof of sensor corruption. BigTIFF, DNG, non-Sony makes, and other RAW
extensions are unsupported. If Make is missing, the `.arw` extension selects
the backend without claiming the manufacturer was verified.

## Known limitations and verification

Only standard common TIFF/EXIF/GPS tags are interpreted. Proprietary/encrypted
Sony MakerNotes, lens corrections, Sony-specific ISO/date overrides, thumbnails,
RAW compression/pixel payloads, XMP sidecars, and ICC contents are not inspected.
A file with readable metadata and damaged or missing sensor data can still
produce a metadata report. This support is not a RAW integrity audit or a
promise that every Sony generation exposes these fields in the same layout.

Tests use original generated containers in
[`tests/fixtures/arw`](../tests/fixtures/arw/README.md), with no camera photograph
or sensor data. They cover both byte orders, common fields and missing/invalid
values, raw/preview dimension selection, every truncated prefix, malformed
lengths/offsets/cycles, mixed collections, JPEG-only filtering, preflight, and
ARW rollback. The regular sanitizer suite includes these tests.

A private camera capture can optionally be tested without committing it:

```sh
PHOTOC_ARW_REAL_FIXTURE=/absolute/path/DSC00001.ARW \
  ctest --test-dir build -R arw_cli --output-on-failure
```

Without that environment variable, the real-camera integration case is skipped.
Generated tests establish supported tag handling, not a camera-model compatibility
matrix. Files are read only; the OS may update access times.

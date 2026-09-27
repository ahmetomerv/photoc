# JPEG and image fixtures

Checked-in binary fixtures for metadata, scanning, scrub/compress, and
sharpness tests. See [`../README.md`](../README.md) for the overall strategy,
provenance, and regeneration commands.

## Catalog

| File | Role | Typical size | Generator |
| --- | --- | --- | --- |
| `no_exif.jpg` | 3×2 RGB JPEG, no APP1 | ~350 B | `make-metadata-fixtures.py` |
| `with_exif.jpg` | Camera EXIF (Model Z, ISO 200, 2026-09-27) | ~550 B | same |
| `with_alt_exif.jpg` | Alternate camera/date (Model A, ISO 100, 2024-01-02) | ~550 B | same |
| `with_gps.jpeg` | EXIF + invented GPS IFD | ~650 B | same |
| `session_1000.jpg` | Capture `2026:09:27 10:00:00` (Model S) | ~550 B | same |
| `session_1030.jpg` | Capture `2026:09:27 10:30:00` | ~550 B | same |
| `session_1200.jpg` | Capture `2026:09:27 12:00:00` | ~550 B | same |
| `invalid.jpg` | Truncated / malformed JPEG | 15 B | same |
| `unsupported.png` | Non-JPEG for unsupported-format paths | ~70 B | same |
| `sharp.jpg` | 128×128 high-contrast checkerboard | ~1 KB | `make-sharpness-fixtures.py` |
| `blurred.jpg` | Box-blurred checkerboard | ~4 KB | same |
| `flat.jpg` | Uniform mid-gray | ~1 KB | same |
| `large_sharp.jpg` | 2048×2048 solid gray (scaled-decode only) | ~17 KB | same |

## Camera metadata matrix

| File | Make | Model | ISO | Aperture | Focal | Captured | GPS |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `no_exif.jpg` | — | — | — | — | — | — | no |
| `with_exif.jpg` | Fixture Camera Co. | Model Z | 200 | 2.8 | 50 | 2026:09:27 12:34:56 | no |
| `with_alt_exif.jpg` | Fixture Camera Co. | Model A | 100 | 4.0 | 35 | 2024:01:02 03:04:05 | no |
| `with_gps.jpeg` | Fixture Camera Co. | Model Z | 200 | 2.8 | 50 | 2026:09:27 12:34:56 | yes |
| `session_*.jpg` | Fixture Camera Co. | Model S | 100 | 4.0 | 35 | see filename | no |

## Duplicates

Exact-duplicate groups are **not** stored here. CLI tests under
`tests/check_duplicates_*.cmake` write short identical / differing byte strings
into a temporary directory so hash and savings math stay obvious and small.

## Naming

- Prefer descriptive stems (`with_exif`, `no_exif`, `invalid`) over camera brand
  names.
- Keep `.jpg` / `.jpeg` / `.JPG` extension variety in *test trees* when covering
  case-insensitive paths; the shared fixtures themselves use the names above.

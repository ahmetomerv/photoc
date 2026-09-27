# Test fixtures

This directory holds small, mostly synthetic inputs and CLI output snapshots used
by the photoc test suite. Prefer regenerating binary images over checking in
real photographs.

## Strategy

| Need | Approach |
| --- | --- |
| EXIF-rich JPEG | Generated: inject APP1 into a tiny 3×2 base (`with_exif.jpg`) |
| No-EXIF JPEG | Generated: 3×2 RGB JPEG with no APP1 (`no_exif.jpg`) |
| GPS JPEG | Generated: same base plus GPS IFD (`with_gps.jpeg`) |
| Malformed JPEG | Generated: truncated SOI/APP1 (`invalid.jpg`) |
| Unsupported type | Generated: 1×1 PNG (`unsupported.png`) |
| Alternate camera metadata | Generated: second make/model/ISO/date (`with_alt_exif.jpg`) |
| Session / capture-time cases | Generated: `session_1000.jpg`, `session_1030.jpg`, `session_1200.jpg` |
| Sharp / blurred / flat | Generated: 128×128 checkerboard and solid fills |
| Large image (decode scaling) | Generated: 2048×2048 solid gray JPEG |
| Duplicate file groups | **Not checked in** — CMake tests write short byte strings at runtime |
| Filesystem path tests | Tiny text file in `fs/` |
| CLI help / report text | Exact stdout snapshots beside this README |

Rules:

1. Keep fixtures small. Metadata JPEGs stay a few hundred bytes; sharpness
   samples stay under a few kilobytes except the dimension-only large file.
2. Prefer generated fixtures. If a checked-in binary can be rebuilt from
   `scripts/`, rebuild it instead of importing a camera original.
3. Never commit photos of people, private GPS tracks, or third-party stock
   images. Invented EXIF strings use the make `Fixture Camera Co.` and models
   `Model A` / `Model Z` / `Model S`.
4. Duplicate scenarios stay ephemeral so the suite does not grow with every
   size/hash combination.

## Layout

```
tests/fixtures/
  README.md                 # this strategy
  jpeg/                     # binary image fixtures (see jpeg/README.md)
  fs/sample.txt             # tiny non-image filesystem fixture
  help-*.txt                # exact CLI help stdout
  exif-*.txt                # exact `photoc exif` stdout snapshots
  stats-*.txt               # exact `photoc stats` stdout snapshots
```

CMake and unit tests point at `tests/fixtures/jpeg` via `FIXTURE_DIR` /
`PHOTOC_*_FIXTURES`. Stable file names are part of the test API; rename only
with a coordinated test update.

## Provenance and licensing

| Artifact | Origin | License notes |
| --- | --- | --- |
| `jpeg/no_exif.jpg` and EXIF variants | Procedural PPM → `cjpeg`, then stdlib APP1 injection (`scripts/make-metadata-fixtures.py`) | Project-owned synthetic test data; no third-party image rights |
| `jpeg/invalid.jpg`, `unsupported.png` | Hand-built bytes in the same script | Same |
| `jpeg/sharp.jpg`, `blurred.jpg`, `flat.jpg`, `large_sharp.jpg` | Procedural pixels → `cjpeg` (`scripts/make-sharpness-fixtures.py`) | Same |
| Help / exif / stats `*.txt` | Captured from photoc CLI output | Same as the tool’s user-facing text |
| Runtime duplicate payloads | Created by CMake `file(WRITE ...)` in tests | Ephemeral; not stored in git |

GPS values in `with_gps.jpeg` are invented (south/west rationals near a
well-known public latitude/longitude) and are not a real capture location.

Older revisions of `no_exif.jpg` once carried a Photoshop APP13 segment from an
editor export. Current fixtures are rebuilt without vendor APP segments.

## Regenerating binary fixtures

```sh
# Metadata + sharpness (requires cjpeg for sharpness, and preferably for the
# no-EXIF base; EXIF variants still rebuild from the embedded fallback).
python3 scripts/make-fixtures.py

# Or individually:
python3 scripts/make-metadata-fixtures.py
python3 scripts/make-sharpness-fixtures.py   # needs cjpeg on PATH
```

After regenerating, run the full suite:

```sh
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure
```

Update `exif-*.txt` / `stats-*.txt` / `help-*.txt` only when the intentional
CLI text changes; those are exact-match snapshots, not generated images.

## Adding a new fixture

1. Decide whether the case needs a checked-in file or can be synthesized in the
   test (prefer synthesis for duplicates, permissions, and temp trees).
2. If checked in, add a generator path under `scripts/` so contributors can
   rebuild it without an external photo.
3. Document the file in `jpeg/README.md` (or this README for non-JPEG assets).
4. Keep dimensions tiny unless a test specifically needs a large canvas.

This directory contains expected CLI help output for exact text tests and a
small text fixture for filesystem tests. Keep help snapshots in sync with
intentional user-facing changes.

`exif-*.txt` are command output snapshots. The command tests substitute the
fixture path and measured file size before comparing stdout exactly.

`stats-*.txt` are report snapshots for flat, recursive, and empty scans.

`jpeg/` contains a 3×2 JPEG without EXIF, variants with camera EXIF and GPS,
an alternate camera/EXIF combination for frequency tests,
an invalid/truncated JPEG, and a PNG for unsupported-format tests. The JPEG
variants and error fixtures can be regenerated with
`python3 scripts/make-metadata-fixtures.py`; the no-EXIF base is checked in.
The `session_*.jpg` variants provide capture times for CLI session plans.
The GPS fixture and the EXIF-free base also verify safe JPEG metadata writing:
non-GPS tags survive, and bytes outside EXIF remain identical.

`sharp.jpg`, `blurred.jpg`, and `flat.jpg` are synthetic 256×256 images for
comparative sharpness tests. `large_sharp.jpg` is 2048×2048 for scaled-decoding
tests. Regenerate them with `python3 scripts/make-sharpness-fixtures.py` when
`cjpeg` is installed; the test suite uses the checked-in JPEGs.

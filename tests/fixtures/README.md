This directory contains expected CLI help output for exact text tests and a
small text fixture for filesystem tests. Keep help snapshots in sync with
intentional user-facing changes.

`exif-*.txt` are command output snapshots. The command tests substitute the
fixture path and measured file size before comparing stdout exactly.

`jpeg/` contains a 3×2 JPEG without EXIF, variants with camera EXIF and GPS,
an invalid/truncated JPEG, and a PNG for unsupported-format tests. The JPEG
variants and error fixtures can be regenerated with
`python3 scripts/make-metadata-fixtures.py`; the no-EXIF base is checked in.

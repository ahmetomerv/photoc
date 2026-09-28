# photoc 0.1.0

First public release of photoc, a command-line application for local photo
collections on macOS and Linux.

## Included commands

- `exif`: read JPEG dimensions, camera and exposure settings, capture time, and GPS.
- `stats`: summarize a JPEG collection's storage, dates, cameras, and exposure settings.
- `duplicates`: find exact file duplicates using SHA-256.
- `rename` and `sort`: preview metadata-based filenames and date/session folders,
  then apply changes with `--apply`.
- `compress`: make JPEG copies at a chosen quality or target size.
- `focus`: compare JPEG sharpness scores as a photo-review aid.
- `scrub`: remove EXIF GPS tags, writing copies by default.

JSON output is available for `exif`, `stats`, `duplicates`, and `focus`.
Shell completions are included for zsh, bash, and fish.

## File safety and current limits

Rename and sort preview by default and never overwrite existing destinations.
Compression is lossy and drops ICC/XMP metadata. Scrub removes EXIF GPS only;
location data in other metadata may remain. `scrub --in-place` replaces
originals without creating a backup. See the
[file-safety notes](https://github.com/ahmetomerv/photoc/blob/v0.1.0/README.md#file-safety)
and command guides before changing or sharing important photos.

Image commands currently support JPEG files. Sharpness scores are review hints,
not proof of blur. `--verbose` and `--quiet` are accepted but do not change
output yet. The command-line interface may change during the `0.x` series;
review each release's notes before upgrading.

## Downloads and requirements

Choose the archive or executable matching your OS and CPU:

- **macOS arm64 or x86_64:** macOS 15 or newer. Install runtime libraries with
  `brew install libexif jpeg-turbo` on the matching architecture.
- **Linux x86_64:** built on Ubuntu 22.04; requires glibc 2.35 or newer,
  libexif (`libexif.so.12`), and TurboJPEG (`libturbojpeg.so.0`). On Ubuntu,
  install them with `sudo apt install libexif12 libturbojpeg`.

Dependency libraries are dynamically linked and are not bundled. macOS
binaries are not developer-signed or notarized. Other CPUs and Linux systems
without glibc should build from source.

Archives contain `bin/photoc`, its man page, shell completions, documentation,
the MIT license, and third-party notices and license texts. `SHA256SUMS` covers
all archives and raw executables;
`SHA256SUMS-<platform>` covers that platform's archive and executable. Verify
downloaded files with `shasum -a 256 -c SHA256SUMS-<platform>` (macOS) or
`sha256sum -c SHA256SUMS-<platform>` (Linux), with both files in that directory.

See the [installation guide](https://github.com/ahmetomerv/photoc/blob/main/docs/installation.md)
for the checksum-verified release installer and safe uninstall flow.

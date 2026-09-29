# photoc 0.2.0

This release adds working global output verbosity and preserves photographic
metadata when recompressing JPEGs. Runtime dependencies and installation
commands are unchanged from 0.1.0.

## Changes since 0.1.0

- `--verbose` / `-v` adds command diagnostics on stderr, including operating
  mode, paths, scan/skip/failure counts, and relevant processing settings.
- `--quiet` / `-q` suppresses informational status and non-critical metadata
  warnings while keeping requested results and errors visible. JSON schemas
  are unchanged, and JSON stdout remains uncontaminated in both modes.
- Quiet and verbose flags work before or after the command. Combining them
  now returns usage status **2**; in 0.1.0 both flags were accepted without
  affecting output.
- `compress` now preserves EXIF APP1, ICC_PROFILE APP2 chunks, and standard
  and Extended Adobe XMP APP1 payloads byte for byte, in their original
  relative order. This applies to quality, target-size, and directory modes.
- Compression validates complete, consistent ICC chunk sets and rejects
  malformed sets without publishing that file's output. Target-size searches
  include all preserved marker overhead. Outputs are decoded and checked for
  dimensions and metadata before publication; grayscale inputs with ICC stay
  grayscale, and EXIF Orientation is retained without rotating pixels.
- Documentation now includes source-install uninstall instructions and clearer
  shell PATH setup.

See the [verbosity guide](https://github.com/ahmetomerv/photoc/blob/v0.2.0/README.md#output-verbosity)
and [metadata support](https://github.com/ahmetomerv/photoc/blob/v0.2.0/docs/compress.md#metadata-preservation)
for exact behavior.

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
Compression is lossy and can increase file size. Preserved EXIF and XMP include
GPS and other location information; compression is not a privacy scrub.
ICC packaging is validated without interpreting or converting the profile.
Standard and Extended XMP are copied as opaque packets; XML, GUID links, and
extended completeness are not validated or repaired. Other original APP
markers, comments, Photoshop/IPTC resources, and MPF/MPO offset data are not
copied. Recognized metadata after the first scan is rejected, and metadata
snapshots are capped at 64 MiB including segment bookkeeping. The existing
decoder does not support CMYK/YCCK JPEGs.

Scrub behavior is unchanged: it removes EXIF GPS only;
location data in other metadata may remain. `scrub --in-place` replaces
originals without creating a backup. See the
[file-safety notes](https://github.com/ahmetomerv/photoc/blob/v0.2.0/README.md#file-safety)
and command guides before changing or sharing important photos.

Image commands currently support JPEG files. Sharpness scores are review hints,
not proof of blur. The command-line interface may change during the `0.x` series;
review each release's notes before upgrading.

## Upgrading

Existing installations do not update automatically. Follow the
[upgrade steps](https://github.com/ahmetomerv/photoc/blob/v0.2.0/docs/installation.md#upgrading)
to replace an owned release installation. For source installations, rebuild
from the new tag and reinstall as documented there. Confirm the update with
`photoc --version`, which should print `photoc 0.2.0`.

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

See the [installation guide](https://github.com/ahmetomerv/photoc/blob/v0.2.0/docs/installation.md)
for the checksum-verified release installer and safe uninstall flow.

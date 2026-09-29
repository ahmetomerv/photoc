# photoc 0.3.0

This release adds JPEG integrity and metadata search, Sony ARW metadata support,
shooting timelines, expanded statistics, privacy metadata removal, and JPEG
contact sheets. Existing `scrub --gps` behavior remains available.

## Changes since 0.2.0

- `check` audits JPEG marker structure, decodes every scanline, and reports
  EXIF and decoder warnings. It is read only and supports human or JSON output.
- `query` searches JPEG metadata with camera, exposure, capture-date, and GPS
  filters. It emits paths, NUL-separated paths, or JSON for scripts.
- `exif`, `stats`, `timeline`, `rename`, and `sort` now read common metadata from
  Sony ARW files. RAW pixels are never developed or rewritten. Other RAW
  formats remain unsupported.
- `stats` adds file-size, exposure, dimension, calendar, and session summaries.
  Existing JSON fields remain; new fields are additive.
- `timeline` groups JPEG and ARW photos by recorded date and capture-time
  session, with configurable gaps and JSON output.
- `scrub --privacy` selectively removes supported EXIF identifiers and GPS,
  standard XMP location and personal fields, and supported IPTC location
  fields. `scrub --all-metadata` removes descriptive metadata while retaining
  ICC profiles and EXIF orientation. The three scrub modes are exclusive.
- `contact` generates paged JPEG contact sheets with filenames and optional
  exposure lines. Thumbnails keep aspect ratio and apply EXIF orientation.
  The built-in text font needs no platform GUI or extra font package.

See the [command guides](https://github.com/ahmetomerv/photoc/tree/v0.3.0/docs)
for exact options, output contracts, and limits.

## File safety and limits

`check`, `query`, `stats`, and `timeline` do not modify photos. `rename` and
`sort` still preview changes unless `--apply` is supplied. `scrub` writes new
copies by default; `--in-place` atomically replaces originals without making
a backup. Contact sheets never overwrite an existing output. If a later page
fails, earlier completed sheets remain. Directory scrub operations are also
not transactional across photos.

Privacy mode does **not** parse or guarantee sanitization of MakerNotes.
Sensitive fields may remain in MakerNotes, unsupported proprietary markers,
filenames, sidecars, or visible image content. Unsupported or malformed XMP
layouts can cause scrub to fail rather than publish a partial edit. Review
outputs before sharing. `--all-metadata` retains ICC and orientation, but
contact sheets copy neither source metadata nor ICC and do not perform color
profile conversion. They are for review, not color-managed proofs.

`check` reports detected readability problems; an OK result is not a backup or
a full metadata/security audit. Contact scans are limited to 1,000 JPEGs,
64 MiB per source file, 24 tiles per page, and a 64 MiB RGB canvas. Large
shoots are paged with `-001`, `-002`, and subsequent filename suffixes.

The CLI remains in the `0.x` series; review each release's notes when
upgrading. There are no intentional removals of existing commands or flags
in this release.

## Upgrading

Installed copies do not update automatically. Follow the
[upgrade steps](https://github.com/ahmetomerv/photoc/blob/v0.3.0/docs/installation.md#upgrading)
to replace an owned release installation. Source installations should be
rebuilt and reinstalled from the new tag. Verify with `photoc --version`,
which should print `photoc 0.3.0`.

## Downloads and requirements

Choose the archive or executable matching your OS and CPU:

- **macOS arm64 or x86_64:** macOS 15 or newer. Install runtime libraries with
  `brew install libexif jpeg-turbo libxml2` on the matching architecture.
- **Linux x86_64:** built on Ubuntu 22.04; requires glibc 2.35 or newer,
  libexif (`libexif.so.12`), TurboJPEG (`libturbojpeg.so.0`), libjpeg
  (`libjpeg.so.8`), and libxml2 (`libxml2.so.2`). On Ubuntu, install them
  with `sudo apt install libexif12 libturbojpeg libjpeg8 libxml2`.

These libraries are dynamically linked and are not bundled. macOS binaries
are not developer-signed or notarized. Other CPUs and Linux systems without
glibc should build from source.

Archives contain `bin/photoc`, its man page, shell completions, documentation,
the MIT license, and third-party notices and license texts. `SHA256SUMS` covers
all archives and raw executables; `SHA256SUMS-<platform>` covers that
platform's archive and executable. Verify downloaded files with
`shasum -a 256 -c SHA256SUMS-<platform>` (macOS) or
`sha256sum -c SHA256SUMS-<platform>` (Linux), with both files in that directory.

See the [installation guide](https://github.com/ahmetomerv/photoc/blob/v0.3.0/docs/installation.md)
for the checksum-verified release installer and safe uninstall flow.

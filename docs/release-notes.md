# photoc 0.8.0

This release brings Sony ARW RAW files to every command except `compress`.
`query`, `focus`, `review`, `contact`, `check`, and `scrub` now handle ARW
alongside JPEG, and ARW metadata from real Sony cameras loads correctly. photoc
still never decodes RAW sensor data: commands that need pixels use the JPEG
preview the camera embeds in every ARW, and no new runtime dependency is
required.

## Changes since 0.7.1

### Sony ARW support

- **`query`** searches ARW metadata with the same filters as JPEG. JSON
  matches include `format` (`jpeg` or `sony_arw`).
- **`focus`** scores an ARW from its embedded preview, area-averaged to the
  size its full-resolution image would be analyzed at, so ARW and JPEG scores
  of the same shot are comparable. On sample files, ARW scores came within
  roughly 8–18% of full-resolution JPEGs of the same shots, with the same
  ordering.
- **`review`** shows an ARW's embedded preview with its orientation applied,
  along with metadata and a sharpness score. ARW bytes are never streamed to
  the terminal; an ARW without a usable preview is shown as unavailable.
- **`contact`** draws ARW thumbnails from the embedded preview. The 64 MiB
  source cap applies to the preview, not to the whole ARW.
- **`check`** audits ARW structure without decoding sensor data: the container
  and metadata, sensor data location and size, every embedded JPEG range, and
  a full decode of the preview. Truncated copies, missing sensor data, and
  damaged previews are errors; a missing preview or unrecognized sensor layout
  is a warning. New codes are documented in the
  [check guide](https://github.com/ahmetomerv/photoc/blob/v0.8.0/docs/check.md).
- **`scrub --gps`** and **`scrub --privacy`** write `DSC00001.scrubbed.ARW`
  copies. Edits are size-preserving, so no sensor data, preview, or MakerNote
  offset moves: GPS and identifier entries are dropped, their values zeroed,
  and private XMP properties removed with the packet padded to its original
  size. Each copy is verified byte for byte before publication. `--in-place`
  and `--all-metadata` are refused for ARW, and privacy mode warns that Sony
  MakerNotes may retain identifiers.
- **`compress`** remains JPEG-only. Directory runs report
  `Sony ARW files skipped: N`, and a single ARW is refused with a clear
  message.

### Fixes

- ARW files whose RAW directory stores TIFF tags out of ascending order, as
  real Sony cameras do, were rejected as "invalid or truncated ARW metadata".
  This affected `exif`, `stats`, `timeline`, `rename`, and `sort` on camera
  files. Tags may now appear in any order; a tag repeated within one directory
  is still rejected.

### Tests and documentation

- Added generated ARW fixtures with embedded previews, thumbnails, and sensor
  strips, and tests for every ARW-capable command, including malformed
  previews, truncation, scrub verification and refusals, and big-endian files.
  An optional private camera file can be tested through
  `PHOTOC_ARW_REAL_FIXTURE`.
- The README, man page, command guides, help text, and shell completions
  describe ARW behavior. [Sony ARW support](https://github.com/ahmetomerv/photoc/blob/v0.8.0/docs/raw.md)
  covers previews, the structural check, and ARW scrubbing.

## Compatibility and safety

Exit codes and runtime dependencies are unchanged. During `0.x`, these output
changes may affect scripts:

- **JSON (additive):** `check` file rows and `query` matches gain a `format`
  field. The `query` summary adds `arw_files_found` and
  `metadata_files_found`, and `query` metadata `width`/`height` are `null` for
  an ARW without recorded dimensions.
- **Human output:** the `focus` heading is now `Sharpness (lowest scores
  first)`; verbose counts for `focus`, `check`, and `scrub` say
  `photos discovered`; usage and unsupported-input messages mention ARW; and
  `compress` adds an ARW skip line to its summary.
- **Directory scans** of `query`, `focus`, `review`, `contact`, `check`, and
  `scrub` now include `.arw` files, so a camera RAW+JPEG pair appears as two
  photos.

File safety is unchanged and extends to ARW: no command modifies an ARW file.
`rename` and `sort` preview unless `--apply` is given; `compress`, `contact`,
and `scrub` write new copies and never overwrite unrelated files; `scrub
--in-place` still replaces original JPEGs with no backup; `review` writes only
its separate state file. See the
[file-safety table](https://github.com/ahmetomerv/photoc/blob/v0.8.0/README.md#file-safety).

## Upgrading

Installed copies do not update automatically. Follow the
[upgrade steps](https://github.com/ahmetomerv/photoc/blob/v0.8.0/docs/installation.md#upgrading)
to replace an owned release installation. Homebrew users can run `brew update`
and `brew upgrade photoc` after the tap formula has been updated for this
release. Source installations should be rebuilt and reinstalled from the new
tag. Verify with `photoc --version`, which should print `photoc 0.8.0`.

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
the MIT license, and third-party notices and license texts. Each platform also
has a dependency manifest (`.dependencies.json`) and an SPDX SBOM
(`.spdx.json`). `SHA256SUMS` covers every asset; `SHA256SUMS-<platform>`
covers that platform's executable, archive, manifest, and SBOM. Verify
downloaded files with `shasum -a 256 -c SHA256SUMS-<platform>` (macOS) or
`sha256sum -c SHA256SUMS-<platform>` (Linux), with both files in that
directory. Build provenance can be checked with
`gh attestation verify photoc-<platform> -R ahmetomerv/photoc`; see
[Provenance and SBOM](https://github.com/ahmetomerv/photoc/blob/v0.8.0/docs/releasing.md#provenance-and-sbom).

See the [installation guide](https://github.com/ahmetomerv/photoc/blob/v0.8.0/docs/installation.md)
for the checksum-verified release installer and safe uninstall flow.

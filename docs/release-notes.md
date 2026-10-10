# photoc 0.7.1

This release focuses on performance and correctness. Scans, hashing, contact
sheets, compression, metadata reads, and interactive review all do less work
for the same results. There are no new commands and no changes to command
options, exit codes, or JSON schemas.

## Changes since 0.6.0

### Performance

- SHA-256 now dispatches at runtime to the CPU's SHA extensions: Intel SHA-NI
  on x86_64 and ARMv8 SHA-2 on AArch64, with the scalar path as the fallback.
  Set `PHOTOC_SHA256_FORCE_SCALAR` in the environment (or define it when
  compiling) to force the scalar path. Digests are unchanged.
- `duplicates` reads 64-byte prefixes for large same-size cohorts through the
  thread pool, keeping the full hashes for actual matches.
- `contact` decodes each source at roughly thumbnail scale instead of up to
  4096 px, while still admitting TurboJPEG's coarsest 1/8 scale for large
  sources. Thumbnails are unchanged; large sources still render.
- `compress` reads the source JPEG once for both metadata and decoding, and
  reuses the chosen encode from a `--target` search instead of re-encoding.
- `image` decoding reuses one TurboJPEG handle across a batch instead of
  creating and destroying one per file.
- `rename` precompiles the `--format` template once per command and renders
  each entry from it rather than re-parsing per photo.
- `stats` uses an indexed lookup for distribution buckets instead of linear
  scans once a table grows.
- JPEG metadata inspection reads EXIF in the single file pass, and entropy
  data is block-scanned rather than read one byte at a time.
- `focus` scores photos in parallel.
- The progress display samples the monotonic clock only as often as the
  90 ms redraw cadence needs, while slow and changing phases still update
  promptly.

### Fixes

- A shared JPEG decode context now re-creates its TurboJPEG handle after a
  rejected header or decode, so a batch keeps working after one bad file on
  older libjpeg-turbo releases (for example Ubuntu 22.04's 2.1.2).
- SHA-256 acceleration now requires every instruction-set feature the
  accelerated block function uses (SHA, SSSE3, and SSE4.1) before it is
  selected. This prevents `SIGILL` on CPUs or VMs that expose SHA while
  masking SSSE3 or SSE4.1.
- EXIF capture allocation failures are now reported as metadata errors
  instead of silently dropping fields.
- The progress update-rate estimate is reset correctly across phase changes.

### Tests and documentation

- Added contact-sheet checks for thumbnail detail, scale, orientation,
  cropping, and blank tiles at both thumbnail-size extremes and for sources
  beyond the old decode cap.
- Added scalar-vs-accelerated hashing coverage, a compress output golden, and
  deterministic progress rendering in the CLI tests.
- Documents `compress` peak memory and the probe cache bound.

## Compatibility and safety

No command, option, exit code, or JSON schema changed in this release, and the
runtime dependencies are unchanged. The SHA-256 acceleration is a transparent
speedup with a scalar fallback, so files hash to the same digests on every CPU.

File-safety behavior is unchanged: `rename` and `sort` preview unless `--apply`
is given; `compress`, `contact`, and `scrub` write new copies and never
overwrite unrelated files; `scrub --in-place` still replaces originals with no
backup; `review` writes only its separate state file and never changes JPEGs.
See the [file-safety table](https://github.com/ahmetomerv/photoc/blob/v0.7.1/README.md#file-safety).

## Upgrading

Installed copies do not update automatically. Follow the
[upgrade steps](https://github.com/ahmetomerv/photoc/blob/v0.7.1/docs/installation.md#upgrading)
to replace an owned release installation. Homebrew users can upgrade after
the tap formula has been updated for this release. Source installations should
be rebuilt and reinstalled from the new tag. Verify with `photoc --version`,
which should print `photoc 0.7.1`.

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

See the [installation guide](https://github.com/ahmetomerv/photoc/blob/v0.7.1/docs/installation.md)
for the checksum-verified release installer and safe uninstall flow.

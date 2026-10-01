# photoc 0.5.0

This release makes file sizes easier to read in terminal output and reorganizes
the documentation. JSON retains exact numeric byte values for scripts.

## Changes since 0.4.0

- `exif`, `stats`, `timeline`, `duplicates`, and `compress` display file sizes
  using decimal B, KB, MB, GB, TB, PB, or EB. Values above B use one decimal
  place, such as `2.3 MB`. This also applies to compression summaries and
  target-miss diagnostics.
- Added shared size formatting and tests for unit boundaries and a 2.3 MB EXIF
  file. JSON byte fields keep their existing names, types, and exact values.
- The README now gives a shorter overview with a demo GIF. Detailed usage,
  installation, scripting, and command references are linked from it.
- Removed duplicate installer and uninstaller files from the repository root.
  The maintained checkout paths are `scripts/install.sh` and
  `scripts/uninstall.sh`; the Quick start still downloads them as
  `install-photoc.sh` and `uninstall-photoc.sh`.

## Compatibility and safety

Human-readable size text has changed. Scripts that need exact byte counts
should use the existing JSON output. There are no JSON schema or exit-code
changes and no new runtime dependencies. Read-only commands remain read only;
file-modifying commands keep their existing overwrite, preview, and rollback
behavior. See the [command guides](https://github.com/ahmetomerv/photoc/tree/v0.5.0/docs)
for their safety and metadata limits.

## Upgrading

Installed copies do not update automatically. Follow the
[upgrade steps](https://github.com/ahmetomerv/photoc/blob/v0.5.0/docs/installation.md#upgrading)
to replace an owned release installation. Source installations should be
rebuilt and reinstalled from the new tag. Verify with `photoc --version`,
which should print `photoc 0.5.0`.

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

See the [installation guide](https://github.com/ahmetomerv/photoc/blob/v0.5.0/docs/installation.md)
for the checksum-verified release installer and safe uninstall flow.

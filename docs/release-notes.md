# photoc 0.4.0

This release makes long-running photo operations easier to follow in an
interactive terminal. It adds a shared progress display and the global
`--no-progress` option. Existing command results, JSON output, and file-safety
behavior are unchanged.

## Changes since 0.3.0

- Directory scans show a spinner during discovery. When the number of photos
  or files is known, processing shows a count and percentage. Duplicate
  detection also distinguishes discovery, candidate grouping, and hashing.
- Progress is available in directory operations for `stats`, `duplicates`,
  `focus`, `compress`, `scrub`, `rename`, `sort`, `query`, `check`, `timeline`,
  and `contact`. Rename and sort show progress during planning and `--apply`.
  Single-file `exif` remains immediate and does not show progress.
- Progress is written to stderr; requested results and JSON stay on stdout.
  Animation starts only when stderr is a terminal and the operation lasts at
  least about 200 ms. Redirected stderr, `--quiet`, and `--no-progress` disable
  the display. JSON alone does not disable it.
- Progress redraws are throttled, completion can show elapsed time, and
  interruption leaves the terminal line clean. Worker threads report counts
  to the caller thread, which alone renders progress.
- The README now uses a banner image, and the installer no longer prints a
  fish-specific PATH suggestion.

There are no intentional command removals, JSON schema changes, or new runtime
dependencies. The global `--no-progress` option works before or after the
command. See the [command guides](https://github.com/ahmetomerv/photoc/tree/v0.4.0/docs)
for exact options and output contracts.

## File safety and limits

Read-only commands remain read only. `rename` and `sort` still preview changes
unless `--apply` is supplied; progress does not change their preflight or
rollback behavior. `scrub` writes new copies by default, while `--in-place`
atomically replaces originals without a backup. Directory scrub operations
are not transactional across photos. Contact sheets do not overwrite an
existing output. See the command guides for the existing metadata, JPEG,
privacy, and color-management limits.

The progress display advances at file or work-unit boundaries. A single
long-running synchronous file operation may leave its current spinner frame
visible until that operation returns. Progress is informational and does not
guarantee that every file can be processed.

## Upgrading

Installed copies do not update automatically. Follow the
[upgrade steps](https://github.com/ahmetomerv/photoc/blob/v0.4.0/docs/installation.md#upgrading)
to replace an owned release installation. Source installations should be
rebuilt and reinstalled from the new tag. Verify with `photoc --version`,
which should print `photoc 0.4.0`.

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

See the [installation guide](https://github.com/ahmetomerv/photoc/blob/v0.4.0/docs/installation.md)
for the checksum-verified release installer and safe uninstall flow.

# photoc 0.6.0

This release adds interactive JPEG review. Browse a shoot in the terminal,
inspect metadata and sharpness, and save picked or rejected marks without
changing the photos.

## Changes since 0.5.0

- Added `photoc review <directory>` with keyboard navigation, pick (`p`),
  reject (`x`), unmark (`u`), metadata detail (`i`), help (`?`), and quit (`q`).
  Letter keys accept either case. The action keys have prominent labels and
  colors that can be disabled with `NO_COLOR=1`.
- Added flat or recursive JPEG discovery, deterministic name or capture-date
  ordering, and `--show all|unmarked|picked|rejected` navigation filters.
  Counts always cover the full discovered collection.
- Added a versioned `.photoc-review.json` state file with atomic saves after
  each changed mark. Review resumes saved selections, retains entries for
  temporarily absent photos, and refuses malformed or wrong-root state files.
- Added `--images auto|iterm|none`. Auto mode uses inline JPEG display in a
  confidently detected direct iTerm2 session and otherwise uses text only.
  `iterm` forces protocol output but cannot make an unsupported terminal show
  images; `none` keeps the metadata and controls without an image. Sharpness
  is computed when first needed and cached for the session.
- Added review coverage for state safety, unusual filenames, ordering, image
  encoding, terminal restoration, and interactive navigation. Updated help,
  the man page, command guide, and shell completions.

## Compatibility and safety

`review` requires a directory and interactive stdin and stdout. It supports
JPEG files only, does not support `--json`, and never edits, moves, or deletes
photos. Its only persistent write is the separate review-state JSON file.
Existing commands, JSON schemas, and runtime dependencies are unchanged.
Review supports macOS and Linux terminals; inline images use iTerm2's protocol,
with a text-only fallback elsewhere. See the [review guide](https://github.com/ahmetomerv/photoc/blob/v0.6.0/docs/review.md)
for controls, state behavior, and terminal limits.

## Upgrading

Installed copies do not update automatically. Follow the
[upgrade steps](https://github.com/ahmetomerv/photoc/blob/v0.6.0/docs/installation.md#upgrading)
to replace an owned release installation. Homebrew users can upgrade after
the tap formula has been updated for this release. Source installations should
be rebuilt and reinstalled from the new tag. Verify with `photoc --version`,
which should print `photoc 0.6.0`.

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

See the [installation guide](https://github.com/ahmetomerv/photoc/blob/v0.6.0/docs/installation.md)
for the checksum-verified release installer and safe uninstall flow.

# photoc

<img src="assets/photoc.svg#gh-dark-mode-only" alt="photoc icon" width="96" height="96">
<img src="assets/photoc-light.svg#gh-light-mode-only" alt="photoc icon" width="96" height="96">

[![Version](https://img.shields.io/github/v/release/ahmetomerv/photoc?label=version)](https://github.com/ahmetomerv/photoc/releases/latest)
[![Tests / CI](https://img.shields.io/github/actions/workflow/status/ahmetomerv/photoc/ci.yml?branch=main&event=push&label=tests%20%2F%20CI)](https://github.com/ahmetomerv/photoc/actions/workflows/ci.yml)
[![Release / CD](https://img.shields.io/github/actions/workflow/status/ahmetomerv/photoc/release.yml?event=push&label=release%20%2F%20CD)](https://github.com/ahmetomerv/photoc/actions/workflows/release.yml)

**Command-line tools for photographers.**

`photoc` is a command-line toolkit for photographers who work with local collections, shell scripts, and repeatable workflows. Inspect metadata, summarize a shoot, find exact duplicates, organize filenames and folders, compress JPEG copies, or remove EXIF GPS tags.

## Installation

### GitHub releases

`scripts/install.sh` installs a checksum-verified release binary for macOS arm64, macOS x86_64, or Linux x86_64 to `$HOME/.local/bin`, without `sudo`:

```sh
sh scripts/install.sh
sh scripts/install.sh --version v0.1.0 --install-dir "$HOME/bin"
```

Release assets become available after a version-tagged build succeeds; build
from source if no suitable release is available. See
[installer usage and release asset names](docs/installation.md)
for downloading the script, PATH setup, requirements, and upgrade behavior.
The installer refuses to replace an existing file or symlink.

### Before building

Install the required packages for your system:

**macOS (Homebrew)**

If needed, install Apple's command-line tools with `xcode-select --install`, then run:

```sh
brew install cmake pkgconf libexif jpeg-turbo
```

**Ubuntu / Debian**

```sh
sudo apt install build-essential git cmake pkg-config libexif-dev libturbojpeg0-dev
```

On other Linux distributions, install the equivalent packages.

### Build from source

```sh
git clone https://github.com/ahmetomerv/photoc.git
cd photoc
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/photoc --version
```

Run `./build/photoc` directly, or install the binary and man page for your user:

```sh
cmake --install build --prefix "$HOME/.local"
```

Add `~/.local/bin` to your `PATH` if needed. The examples below assume `photoc`
is on `PATH`; otherwise use `./build/photoc`. Keep the dependency libraries
installed when using the binary.

The default install locations are `bin/photoc`, `share/man/man1/photoc.1`,
and license notices under `share/doc/photoc` beneath the prefix.
Configure `CMAKE_INSTALL_BINDIR`, `CMAKE_INSTALL_MANDIR`, or `CMAKE_INSTALL_DOCDIR`
to use a different layout. Read the [man page](man/photoc.1) with `man photoc`,
or directly from the checkout with `man ./man/photoc.1`. If your manual search
path does not include the user prefix, use
`man -M "$HOME/.local/share/man" photoc`.

### Updating after local changes

After editing the source, run these commands from the repository root:

```sh
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Rebuilding updates `./build/photoc`, which you can run immediately. The installed
copy is separate: after the tests pass, update a source installation with:

```sh
cmake --install build --prefix "$HOME/.local"
```

Use the same installation prefix as before. With this prefix, the updated
executable is `$HOME/.local/bin/photoc`. Check which copy your shell runs with
`command -v photoc`.

If you change CMake options or dependencies, rerun the configure command from
[Build from source](#build-from-source), including your chosen options, before
rebuilding.

### Shell completions

Optional scripts for **zsh, bash, and fish** complete commands, applicable
options, common option values, and paths. See
[completion installation](completions/README.md) for per-user setup. They use
native shell facilities and add no runtime dependency.

### Uninstall

For a release installed by `scripts/install.sh`, preview and then apply:

```sh
sh scripts/uninstall.sh
sh scripts/uninstall.sh --apply
```

Use `--install-dir` for a custom location. The script verifies the installer's
ownership receipt and removes only the unchanged executable and receipt.
Configuration, shell setup, completions, man pages, and unrelated files remain.
See [uninstall details](docs/installation.md#uninstalling-a-tracked-release-installation)
for downloading the script and handling untracked source/manual installs.

## Quick start

```sh
photoc --help
photoc exif photo.jpg
photoc stats ./photos --recursive
```

Use `photoc <command> --help` for usage and options. Directory scans include
one level by default; `--recursive` includes nested directories. Scans do not
follow symlinks. Use `--` before a path that begins with `-`, and quote paths
containing spaces.

## Commands

| Command | Purpose | Default behavior |
| --- | --- | --- |
| [`compress`](docs/compress.md) | Re-encode JPEGs by quality or target size | Write new copies |
| [`exif`](docs/exif.md) | Inspect dimensions, camera, exposure, capture time, and GPS | Read only; JSON supported |
| [`duplicates`](docs/duplicates.md) | Report SHA-256 duplicate groups and potential savings | Read only; JSON supported |
| [`stats`](docs/stats.md) | Summarize storage, capture dates, and camera/exposure distributions | Read only; JSON supported |
| [`rename`](docs/rename.md) | Rename JPEGs using metadata templates | Dry run; `--apply` to rename |
| [`sort`](docs/sort.md) | Organize JPEGs by capture date or session | Dry run; `--apply` to move |
| [`focus`](docs/focus.md) | Compare JPEG sharpness scores | Read only; lowest scores first; JSON supported |
| [`scrub`](docs/scrub.md) | Remove EXIF GPS tags from JPEGs | Write new copies |

Follow each command link for syntax, options, examples, edge cases, safety
notes, and JSON schemas where supported.

### Examples

```sh
# Compression: quality defaults to 80; quality and target are alternatives.
photoc compress photo.jpg --quality 75
photoc compress photo.jpg --target 2MB --min-quality 30
photoc compress ./photos --recursive --output-dir ./compressed

# Read metadata, find duplicates, and summarize a collection.
photoc exif photo.jpg
photoc duplicates ./photos --recursive
photoc stats ./photos --recursive

# Preview renames and moves; these commands do not modify files by default.
photoc rename ./photos --format "{date}_{camera}_{sequence}.{ext}"
photoc sort ./photos --by date --recursive
photoc sort ./photos --by session --gap 30m

photoc focus photo.jpg
photoc focus ./photos --recursive --threshold 100 --only-blurry

# Create photo.scrubbed.jpg with EXIF GPS tags removed.
photoc scrub photo.jpg --gps
photoc scrub ./photos --gps --recursive
```

Rename templates support `{date}`, `{datetime}`, `{camera}`, `{make}`, `{iso}`,
`{aperture}`, `{focal}`, `{sequence}`, `{original}`, and `{ext}`. Unsafe filename
characters are sanitized; sequence numbers start at `0001` in source-path
order, and `{ext}` preserves the original extension's casing. Missing metadata
required by a template blocks that rename.

Date sorting uses `YYYY/MM/DD/`; session sorting uses `session-001/`,
`session-002/`, and so on. The default session gap is 60 minutes; `--gap 30m`
and `--gap 2h` override it. Missing capture dates are reported and blocked.

Stats omit unavailable metadata from distributions and sort counts by
frequency with deterministic ties. Scans can continue past individual file
errors, so check warnings and scan summaries when completeness matters.

## File safety

- **Rename and sort:** preview first, then add `--apply` to the same command.
  The entire plan is checked before changes; a blocked entry prevents the
  apply. Existing destinations are never overwritten. Failures during apply
  trigger rollback attempts, but concurrent filesystem changes can prevent
  full restoration. Keep backups of important collections.
- **Compress:** originals stay unchanged; outputs are named
  `photo.compressed.jpg`. Existing outputs are skipped in directory mode and
  rejected in single-file mode. Compression is lossy and can produce a larger
  file. EXIF, including GPS, is preserved where possible; ICC and XMP segments
  are not copied. An unreachable size target writes a best-effort copy at the
  minimum quality and returns exit status 1. `MB` is decimal; `MiB` is binary.
- **Scrub:** originals stay unchanged by default, and existing copy destinations
  are rejected. JPEG image data is preserved without recompression; non-GPS
  EXIF is retained where libexif can represent it. Unusual MakerNotes may
  change. GPS data in XMP or MakerNotes is outside this command's scope.
- **Explicit in-place scrub:** `photoc scrub photo.jpg --gps --in-place`
  verifies a temporary file before atomic replacement. **It creates no backup.**
  Unsafe sources, including symlinks, hard links, and changed files, are refused.
  POSIX permission bits and group ownership are preserved; ACLs and extended
  attributes are not copied.

## JSON output

Available for `exif`, `stats`, `duplicates`, and `focus`:

```sh
photoc exif photo.jpg --json
photoc stats ./photos --recursive --json > stats.json
photoc duplicates ./photos --recursive --json > duplicates.json
photoc focus ./photos --recursive --json > focus.json

# Optional: use jq to select exposure metadata.
photoc exif photo.jpg --json | jq '.exposure'
```

Example exposure object:

```json
{
  "iso": 200,
  "aperture": 2.8,
  "exposure_time_seconds": 0.008,
  "focal_length_mm": 50
}
```

Unavailable values are `null`; presence fields such as `has_gps` are booleans,
and numeric values stay numeric. JSON mode emits no human-readable decoration
on stdout; warnings still go to stderr. `jq` is optional, not a photoc dependency.

Exit statuses: **0** success, **1** operational failure, **2** invalid usage,
**3** unimplemented command. `stats` can finish successfully with per-file
warnings; its scan summary records those errors. `--verbose` and `--quiet`
are recognized but do not change output yet.

## Contributing

Bug reports, documentation improvements, and focused patches are welcome.
See [CONTRIBUTING.md](CONTRIBUTING.md) for tests, sanitizers, and static analysis,
[AGENTS.md](AGENTS.md) for repository conventions, and
[benchmarks/README.md](benchmarks/README.md) for reproducible performance checks.
Shared C APIs and ownership rules are documented in [include/photoc](include/photoc).
Please follow the [code of conduct](CODE_OF_CONDUCT.md). For file corruption,
unsafe overwrites, or suspected vulnerabilities, use the private reporting
guidance in [SECURITY.md](SECURITY.md).
See the [first-release audit](docs/release-audit.md) for verification results,
remaining limits, and release gates.

## License

photoc's source is licensed under the [MIT License](LICENSE).
Dependencies retain their own licenses; see [third-party notices](THIRD_PARTY_NOTICES.md).
This software is based in part on the work of the Independent JPEG Group.

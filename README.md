# photoc

**Unix-style photography tools for the terminal.**

`photoc` is a command-line toolkit written in C17 for photographers who work
with local collections, shell scripts, and repeatable workflows. Inspect
metadata, summarize a shoot, find exact duplicates, organize filenames and
folders, compress JPEG copies, or remove EXIF GPS tags.

The philosophy is simple: focused commands, readable output, small
dependencies, and explicit file changes. Normal output goes to stdout;
diagnostics go to stderr. Selected commands also provide JSON for scripts.

## Platforms and scope

- **macOS and Linux** are the initial supported platforms.
- Image and metadata operations currently support **JPEG** (`.jpg` and `.jpeg`,
  case-insensitive). RAW, HEIC, and other image formats are not supported yet.
- `duplicates` compares regular files of **any type**, by exact file contents.
  It does not detect visually similar images.
- `focus` is **not implemented**. A shared sharpness metric exists, but there is
  no working focus command or focus JSON output yet.

## Installation

### Homebrew tap

A formula for building tagged source, including the man page and shell
completions, is prepared for an independent tap. Once the tap is published,
install with `brew install <owner>/tap/photoc`. See
[tap setup and installation](docs/homebrew.md). No homebrew-core inclusion is
assumed.

### GitHub releases

`scripts/install.sh` installs a checksum-verified release binary for macOS
arm64, macOS x86_64, or Linux x86_64 to `$HOME/.local/bin`, without `sudo`:

```sh
sh scripts/install.sh
sh scripts/install.sh --version v0.1.0 --install-dir "$HOME/bin"
```

Release assets become available after a version-tagged build succeeds; build
from source if no suitable release is available. See
[installer usage and release asset names](docs/installation.md)
for downloading the script, PATH setup, requirements, and upgrade behavior.
The installer refuses to replace an existing file or symlink.

### Source dependencies

Build from source using a C17 compiler (Clang or GCC), CMake **3.21+**,
`pkg-config`, [libexif](https://libexif.github.io/), and
[libjpeg-turbo](https://libjpeg-turbo.org/) with its TurboJPEG development files.
Dependencies are installed through the system package manager, not vendored.

**macOS — Homebrew**

Install Xcode Command Line Tools if needed (`xcode-select --install`), then:

```sh
brew install cmake pkgconf libexif jpeg-turbo
```

**Debian / Ubuntu**

```sh
sudo apt install build-essential git cmake pkg-config libexif-dev libturbojpeg0-dev
```

Other Linux distributions need the equivalent development packages for libexif
and TurboJPEG.

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

The default install locations are `bin/photoc` and `share/man/man1/photoc.1`
under the prefix. Configure `CMAKE_INSTALL_BINDIR` or `CMAKE_INSTALL_MANDIR`
to use a different layout. Read the [man page](man/photoc.1) with `man photoc`,
or directly from the checkout with `man ./man/photoc.1`. If your manual search
path does not include the user prefix, use
`man -M "$HOME/.local/share/man" photoc`.

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
| [`focus`](docs/focus.md) | Planned sharpness analysis | Not implemented; exit status 3 |
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

# Planned syntax only: currently reports "not implemented".
photoc focus photo.jpg

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

Available for `exif`, `stats`, and `duplicates`:

```sh
photoc exif photo.jpg --json
photoc stats ./photos --recursive --json > stats.json
photoc duplicates ./photos --recursive --json > duplicates.json

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

## Roadmap

Planned work, without release dates:

- Expose the existing sharpness metric through `focus`, with human and JSON
  output. Sharpness measures edge variation, not artistic quality.
- Broaden release compatibility and add package-manager distribution.
- Evaluate additional image formats while retaining safe file handling.

## Contributing

Bug reports, documentation improvements, and focused patches are welcome.
See [CONTRIBUTING.md](CONTRIBUTING.md) for tests, sanitizers, and static analysis,
[AGENTS.md](AGENTS.md) for repository conventions, and
[benchmarks/README.md](benchmarks/README.md) for reproducible performance checks.
Shared C APIs and ownership rules are documented in [include/photoc](include/photoc).

## License

photoc's source is licensed under the [MIT License](LICENSE).
Dependencies retain their own licenses.

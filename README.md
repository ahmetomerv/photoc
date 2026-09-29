# photoc

<img src="assets/photoc.svg#gh-dark-mode-only" alt="photoc icon" width="96" height="96">
<img src="assets/photoc-light.svg#gh-light-mode-only" alt="photoc icon" width="96" height="96">

[![Version](https://img.shields.io/github/v/release/ahmetomerv/photoc?label=version)](https://github.com/ahmetomerv/photoc/releases/latest)
[![Tests / CI](https://img.shields.io/github/actions/workflow/status/ahmetomerv/photoc/ci.yml?branch=main&event=push&label=tests%20%2F%20CI)](https://github.com/ahmetomerv/photoc/actions/workflows/ci.yml)
[![Release / CD](https://img.shields.io/github/actions/workflow/status/ahmetomerv/photoc/release.yml?event=push&label=release%20%2F%20CD)](https://github.com/ahmetomerv/photoc/actions/workflows/release.yml)

**Command-line tools for photographers.**

`photoc` helps you manage photos on your computer from the terminal. Read photo
metadata, summarize a shoot, find exact duplicates, rename and organize photos,
make compressed copies, or remove EXIF GPS tags. You can also use it in shell
scripts to repeat these tasks.

## Installation

### GitHub releases

Use this option to install a prebuilt executable without a compiler or `sudo`.
The installer supports **macOS arm64**, **macOS x86_64**, and **Linux x86_64**.
Current release builds require macOS 15 or later, or glibc-based Linux with
glibc 2.35 or later. Check the release notes for any additional requirements.

1. Install the libraries needed to run photoc:

   ```sh
   # macOS (Homebrew)
   brew install libexif jpeg-turbo

   # Ubuntu / Debian
   sudo apt install libexif12 libturbojpeg libjpeg8
   ```

2. Download and run the installer:

   ```sh
   curl -fsSL https://raw.githubusercontent.com/ahmetomerv/photoc/main/scripts/install.sh \
     -o install-photoc.sh
   sh install-photoc.sh
   ```

   It checks the download's SHA-256 checksum and installs the latest release
   to `$HOME/.local/bin/photoc`. It requires `curl`, standard Unix utilities,
   and either `sha256sum` or `shasum`.

3. Follow [Add photoc to your PATH](#add-photoc-to-your-path), then check the
   installation:

   ```sh
   photoc --version
   ```

To choose a published version and a different install directory:

```sh
sh install-photoc.sh --version v0.2.0 --install-dir "$HOME/bin"
```

The chosen tag must exist as a published release. If you already have a checkout
of this repository, use `sh scripts/install.sh` instead of downloading the script;
the same options work with it.

The installer refuses to replace an existing file or symlink. To upgrade a
release installation, [uninstall the old copy](#uninstall) before installing the
new one. It installs the executable and an ownership receipt; use the source
installation for the man page and the separate [completion setup](#shell-completions)
for shell completions.

Find available versions on the [GitHub releases page](https://github.com/ahmetomerv/photoc/releases).
If there is no release for your platform, [build from source](#build-from-source).
See [installation details](docs/installation.md) for supported release files,
requirements, and upgrade behavior.

### Build from source

Run these steps in your terminal:

1. Install the build tools and libraries for your system.

   **macOS (Homebrew):** if needed, run `xcode-select --install` to install
   Apple's command-line tools, then run:

   ```sh
   brew install cmake pkgconf libexif jpeg-turbo
   ```

   **Ubuntu / Debian:**

   ```sh
   sudo apt install build-essential git cmake pkg-config libexif-dev libturbojpeg0-dev libjpeg-dev
   ```

   On other Linux distributions, install the equivalent packages.

2. Download the source and enter the project directory:

   ```sh
   git clone https://github.com/ahmetomerv/photoc.git
   cd photoc
   ```

3. Configure, build, and test photoc:

   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ctest --test-dir build --output-on-failure
   ./build/photoc --version
   ```

4. Run `./build/photoc` directly, or install it for your user:

   ```sh
   cmake --install build --prefix "$HOME/.local"
   ```

   This installs the executable, man page, and license notices under
   `$HOME/.local`. Keep the dependency libraries installed so photoc can run.

#### Uninstall a source installation

`cmake --install` does not create the ownership receipt used by
`scripts/uninstall.sh`. That script handles release installations made by
`install.sh`; for a source installation, it reports
`no regular ownership receipt` and removes nothing.

1. Check which executable your shell uses and review the installed file list
   from the build directory you used:

   ```sh
   command -v photoc
   cat build/install_manifest.txt
   ```

2. If you installed with the `$HOME/.local` prefix above and the executable
   is `$HOME/.local/bin/photoc`, remove that installed copy:

   ```sh
   rm "$HOME/.local/bin/photoc"
   ```

   Use the actual installed path if you chose a different prefix or layout.
   Check that it still belongs to your source installation before removing it.

This removes only the installed executable. For a full uninstall, review and
remove the installed man page and license files listed in the manifest too.
Your source code and `build/` directory remain available. If you only ran
`./build/photoc` without installing it, there is no installed copy to remove.
See [source uninstall details](docs/installation.md#uninstalling-a-source-or-manual-installation)
for custom paths and remaining files.

### Add photoc to your PATH

The examples below use `photoc` directly. If your shell cannot find it, add
`$HOME/.local/bin` to `PATH`.

For **bash or zsh**, run this line now and add it to your shell startup file
(`~/.bashrc` or `~/.zshrc`) for future sessions:

```sh
export PATH="$HOME/.local/bin:$PATH"
```

Use your chosen directory instead if you installed somewhere else. If you
built from source and prefer to skip installation, replace `photoc` in the
examples with `./build/photoc` while working from the repository root.

### Read the manual

A source installation puts files in these locations under your install prefix:

- Executable: `bin/photoc`
- Man page: `share/man/man1/photoc.1`
- License notices: `share/doc/photoc`

To change these locations, configure `CMAKE_INSTALL_BINDIR`,
`CMAKE_INSTALL_MANDIR`, or `CMAKE_INSTALL_DOCDIR` when building.

Read the [man page](man/photoc.1) with `man photoc`. If your shell cannot find
the installed manual, use `man -M "$HOME/.local/share/man" photoc`. From a
checkout, use `man ./man/photoc.1`.

### Update a release installation

Installed copies do not update automatically. To install the latest release:

1. Check your version with `photoc --version` and read the
   [release notes](https://github.com/ahmetomerv/photoc/releases).
2. Follow the [uninstall steps](#uninstall), including the preview.
3. Run the installer again using the [GitHub release steps](#github-releases).
4. Run `photoc --version` to check the result.

Use the same install directory as before. To select a particular release,
pass `--version v0.2.0` to the installer. See the
[upgrade guide](docs/installation.md#upgrading) for copyable commands and how
to return to an earlier version.

### Updating after local changes

After editing the source:

1. Rebuild and test from the repository root:

   ```sh
   cmake --build build --parallel 2
   ctest --test-dir build --output-on-failure
   ```

   This updates `./build/photoc`, which you can run immediately.

2. If you installed from source, update the installed copy after the tests pass:

   ```sh
   cmake --install build --prefix "$HOME/.local"
   ```

   Use the same prefix as before. With this prefix, the updated executable is
   `$HOME/.local/bin/photoc`.

3. Check which copy your shell runs:

   ```sh
   command -v photoc
   ```

If you change CMake options or dependencies, rerun the configure command from
[Build from source](#build-from-source), including your chosen options, before
rebuilding.

### Shell completions

Optional scripts for **zsh, bash, and fish** let you press Tab to complete
commands, supported options, common option values, and paths. Follow the
[completion installation steps](completions/README.md) for your shell.
They use built-in shell features and need no extra runtime dependency.

### Uninstall

For a release installed with the installer:

1. Download the uninstaller, or use `scripts/uninstall.sh` from a checkout:

   ```sh
   curl -fsSL https://raw.githubusercontent.com/ahmetomerv/photoc/main/scripts/uninstall.sh \
     -o uninstall-photoc.sh
   ```

2. Preview what will be removed:

   ```sh
   sh uninstall-photoc.sh
   ```

3. After checking the preview, remove the installed copy:

   ```sh
   sh uninstall-photoc.sh --apply
   ```

If you use the checkout script, the equivalent commands are
`sh scripts/uninstall.sh` and `sh scripts/uninstall.sh --apply`.
For a custom location, pass `--install-dir "$HOME/bin"` to both the preview
and removal commands, using the directory you originally chose.

The script checks the installer's ownership receipt. It removes only the
unchanged executable and that receipt. It leaves configuration, shell setup,
completions, man pages, and unrelated files in place.

Source and manual installations without a receipt cannot be removed by this
script. Follow [Uninstall a source installation](#uninstall-a-source-installation)
or the [source/manual uninstall guide](docs/installation.md#uninstalling-a-source-or-manual-installation)
instead.

## Quick start

Start with these read-only commands. Replace `photo.jpg` and `./photos` with
your own file and directory paths.

1. See the available commands: `photoc --help`
2. Read one photo's metadata: `photoc exif photo.jpg`
3. Summarize a folder and its subfolders: `photoc stats ./photos --recursive`

For help with a specific command, run `photoc <command> --help`, such as
`photoc compress --help`.

Directory scans look only inside the chosen folder by default. Add
`--recursive` to include subfolders. Scans do not follow symlinks.
Quote paths containing spaces, such as `photoc exif "Summer trip/photo.jpg"`.
Put options before `--` when a path begins with `-`, for example
`photoc exif -- -photo.jpg`.

## Commands

| Command | Purpose | Default behavior |
| --- | --- | --- |
| [`query`](docs/query.md) | Search JPEG metadata using combined filters | Read only; paths, NUL paths, or JSON |
| [`check`](docs/check.md) | Audit JPEG structural readability and report warnings/errors | Read only; JSON supported |
| [`compress`](docs/compress.md) | Compress JPEGs at a chosen quality or target file size | Write new copies |
| [`exif`](docs/exif.md) | Inspect dimensions, camera, exposure, capture time, and GPS | Read only; JSON supported |
| [`duplicates`](docs/duplicates.md) | Find files with identical bytes using SHA-256 and show potential space savings | Read only; JSON supported |
| [`stats`](docs/stats.md) | Summarize storage, capture dates, cameras, and exposure settings | Read only; JSON supported |
| [`rename`](docs/rename.md) | Rename JPEG/ARW photos using metadata templates | Preview; `--apply` to rename |
| [`sort`](docs/sort.md) | Organize JPEG/ARW photos by capture date or session | Preview; `--apply` to move |
| [`focus`](docs/focus.md) | Compare JPEG sharpness scores | Read only; lowest scores first; JSON supported |
| [`scrub`](docs/scrub.md) | Remove EXIF GPS tags from JPEGs | Write new copies |

Each command link includes usage, options, examples, special cases, safety
notes, and a JSON schema when the command supports JSON.
`check`, `query`, and Sony ARW metadata support are currently available in
source builds; they are not included in v0.2.0.

### File-type support

| Command | JPEG | Sony ARW | Other RAW files |
| --- | --- | --- | --- |
| `exif`, `stats`, `rename`, `sort` | Metadata | Common TIFF/EXIF metadata | Unsupported/skipped |
| `query`, `check`, `compress`, `focus`, `scrub` | Supported | Unsupported/skipped | Unsupported/skipped |
| `duplicates` | Exact bytes | Exact bytes | Exact bytes |

ARW support is **metadata only**: no RAW development, pixel decoding, compression,
or GPS rewriting. Missing fields remain unavailable. See
[Sony ARW metadata support](docs/raw.md) for fields, safety limits, and fixtures.

## Examples

### Inspect Sony RAW metadata

```sh
photoc exif DSC00001.ARW --json
photoc stats ./photos --recursive
photoc rename ./photos --format '{date}_{camera}_{sequence}.{ext}'
photoc sort ./photos --by date
```

`exif`, `stats`, `rename`, and `sort` discover `.jpg`, `.jpeg`, and `.arw`
case-insensitively.
Rename and sort still preview by default; review the plan before adding
`--apply`. `{ext}` keeps the source extension and its case.

### Search photo metadata

```sh
photoc query ./photos --recursive --iso ">800" --aperture "<=4"
photoc query ./photos --camera "DSC-RX100M7A"
photoc query ./photos --after 2026-01-01 --before 2026-12-31
photoc query ./photos --recursive --has-gps --print0 |
  xargs -0 sh -c 'for path do photoc exif -- "$path" || exit; done' sh
photoc query ./photos --recursive --json | jq '.summary'
```

All filters are ANDed. Camera/make matching is exact and case-sensitive; date
bounds include the named days. Missing queried fields do not match. Default
stdout contains only sorted paths; use `--print0` for arbitrary filenames or
`--json` for metadata and scan counts. See [query details](docs/query.md).

### Check photographs for reading problems

```sh
photoc check photo.jpg
photoc check ./photos --recursive --only-errors
photoc check ./photos --recursive --json > check.json
```

The command parses JPEG structure and decodes the pixels without changing files.
Warnings, including malformed EXIF or decoder recovery, return **0**; errors
or incomplete checks return **1**. `--only-errors` hides warning and OK rows;
summary counts cover every checked file. See [check details](docs/check.md)
for reason codes and limits. Successful decoding is not proof of visual quality
or complete metadata validity.

### Compress JPEG copies

Choose a quality level or a target file size. These are separate modes:
do not combine `--quality` and `--target`. The default quality is **80**.

```sh
# Use a quality level of 75.
photoc compress photo.jpg --quality 75

# Try to fit within 2 MB, without going below quality 30.
photoc compress photo.jpg --target 2MB --min-quality 30

# Compress a folder and its subfolders into a separate output folder.
photoc compress ./photos --recursive --output-dir ./compressed
```

The original stays unchanged. Copies use names such as `photo.compressed.jpg`.
Read the [compression safety notes](#file-safety) before choosing quality or
sharing the copies.

### Read metadata and inspect a collection

```sh
photoc exif photo.jpg
photoc duplicates ./photos --recursive
photoc stats ./photos --recursive
```

These commands do not change files. `duplicates` finds exact file matches.
`stats` reports camera/lens usage, exposure settings (including shutter speed
and explicit 35mm-equivalent focal length), orientation, resolution/megapixels,
calendar activity, sessions, and average/median file size. Missing fields are
excluded from each statistic and counted as unavailable. Session grouping uses
the existing 60-minute gap rule. See [statistics definitions](docs/stats.md).
Counts sort from most common to least common, with a fixed order for ties. Scans may continue after an individual file fails, so check
warnings and the scan summary if you need a complete result.

### Rename photos

First, preview the new filenames:

```sh
photoc rename ./photos --format "{date}_{camera}_{sequence}.{ext}"
```

After reviewing the preview, repeat the same command with `--apply`:

```sh
photoc rename ./photos --format "{date}_{camera}_{sequence}.{ext}" --apply
```

Templates support `{date}`, `{datetime}`, `{camera}`, `{make}`, `{iso}`,
`{aperture}`, `{focal}`, `{sequence}`, `{original}`, and `{ext}`.

- Characters that are unsafe in filenames are replaced with underscores.
- Sequence numbers start at `0001`, ordered by source path.
- `{ext}` keeps the original extension's uppercase or lowercase spelling.
- If a photo lacks metadata needed by the template, its rename is blocked.

See [rename details](docs/rename.md) for placeholder values and more examples.

### Sort photos into folders

Preview folders based on capture date or sessions:

```sh
photoc sort ./photos --by date --recursive
photoc sort ./photos --by session --gap 30m
```

After reviewing a preview, repeat that command with `--apply`. For example:

```sh
photoc sort ./photos --by date --recursive --apply
```

- Date sorting uses folders such as `YYYY/MM/DD/`.
- Session sorting uses `session-001/`, `session-002/`, and so on. A new session
  starts when the time between consecutive photos exceeds the chosen gap.
- The default session gap is **60 minutes**. Use `--gap 30m` for 30 minutes
  or `--gap 2h` for two hours.
- Photos without a valid capture date are reported and blocked.

### Compare sharpness

```sh
photoc focus photo.jpg
photoc focus ./photos --recursive --threshold 100 --only-blurry
```

Results show the lowest sharpness scores first. The second command shows only
photos scoring below 100. Scores help you choose photos to review; they are
not a final judgment of blur. Subject detail and noise can affect the result.
See [focus details](docs/focus.md) for the scoring limits.

### Remove EXIF GPS tags

```sh
photoc scrub photo.jpg --gps
photoc scrub ./photos --gps --recursive
```

If EXIF GPS tags are present, photoc creates a copy such as
`photo.scrubbed.jpg` and keeps the original. Photos without these tags are
skipped without creating a copy. This removes EXIF GPS tags only; location
data in other metadata may remain.

## File safety

- **Check:** reads JPEGs without changing file contents or metadata. A warning
  can mean the decoder recovered missing data; an OK result is not a backup
  or visual-quality guarantee. See [audit limits](docs/check.md#safety-performance-and-limits).
- **Rename and sort:** preview first, then add `--apply` to the same command.
  photoc checks the whole plan before changing files. If any entry is blocked,
  no changes are applied. Existing destination files are never overwritten.
  If a rename or move fails, photoc tries to undo earlier changes. Another
  process changing files at the same time can prevent a full recovery.
  Keep backups of important collections.
- **Compress:** originals stay unchanged; outputs are named
  `photo.compressed.jpg`. Existing outputs are skipped in directory mode and
  rejected in single-file mode. Compression loses image detail and can produce
  a larger file. EXIF (including orientation and GPS), ICC color profiles, and
  standard/Extended XMP marker payloads are preserved byte for byte by default.
  Incomplete or inconsistent ICC chunk sets fail without creating that copy.
  Other original APP markers are not copied; see
  [metadata support and limits](docs/compress.md#metadata-preservation).
  If the target size cannot be reached, photoc
  still writes a copy at the minimum quality and returns exit status **1**.
  `2MB` means 2,000,000 bytes; `2MiB` means 2,097,152 bytes.
- **Scrub:** originals stay unchanged by default, and existing copy destinations
  are rejected. JPEG image data is copied without recompression. Other EXIF
  fields are kept where the libexif library can represent them. Unusual
  camera-specific metadata (MakerNotes) may change. GPS data in XMP or
  MakerNotes is not removed by this command.
- **Explicit in-place scrub:** `photoc scrub photo.jpg --gps --in-place`
  replaces the original after checking a temporary copy. It replaces the file
  in one step (an atomic replacement). **It creates no backup.** Unsafe sources,
  including symlinks, hard links, and files that changed during processing, are
  refused. POSIX permission bits and group ownership are kept; access control
  lists (ACLs) and extended attributes are not copied.

## JSON output

Add `--json` to `query`, `check`, `exif`, `stats`, `duplicates`, or `focus`
to get output for scripts and other tools. You can also save it to a file:

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

Missing values are `null`. Fields such as `has_gps` use `true` or `false`, and
numbers stay numeric. In JSON mode, stdout contains only JSON; diagnostics,
warnings, and errors use stderr. Quiet mode suppresses non-critical warnings.
`jq` is optional and is not required to run photoc.

## Exit codes and output

Normal output goes to stdout. Errors and warnings go to stderr.

| Code | Meaning |
| --- | --- |
| `0` | Success |
| `1` | A processing or filesystem operation failed, or a compression target was not met |
| `2` | Invalid command usage |
| `3` | The command is not implemented |

`stats` can return success even when individual files produce warnings.
Its normal scan summary and JSON `scan.errors` record those errors. When
completeness matters, use normal output or inspect `--json`, including in quiet
mode. `query` returns **1** when a JPEG load fails, while still listing successful
directory matches; no matches returns **0**.

## Output verbosity

Global flags work before or after the command:

| Mode | Behavior |
| --- | --- |
| Default | Show the existing results, operation summaries, and warnings. |
| `-q`, `--quiet` | Keep requested results; suppress status text, operation summaries, and non-critical warnings. Errors explaining a non-zero exit remain on stderr. |
| `-v`, `--verbose` | Keep normal output and add diagnostics on stderr: operating mode, recursion, discovery/skip/failure counts, and relevant paths. Statistics and duplicate scans also report the worker limit; small workloads or worker startup failures can run serially. |

Quiet mode keeps EXIF fields, statistics and distributions, duplicate groups
and savings, focus scores, query paths, check rows/counts, and rename/sort
mappings. It hides scan status and rename/sort summaries. Successful `compress`
and `scrub` operations have no stdout in quiet mode; their file operations and
exit codes stay the same.
Warnings that explain failures, including partial duplicate/focus results or
blocked rename/sort plans, remain visible.

With `--json`, stdout remains JSON only, with the same fields in every mode.
Verbose diagnostics use stderr. Quiet JSON still includes scan/error counters.
Help and version output remain available in every mode. Combining quiet and
verbose is a usage error (exit **2**), regardless of option order.

```sh
# Keep the requested duplicate report, suppress scan status.
photoc duplicates ./photos --quiet

# Save JSON and diagnostics separately.
photoc --verbose stats ./photos --recursive --json > stats.json 2> diagnostics.log

# Remove GPS in new copies, showing only failures.
photoc scrub ./photos --gps --quiet
```

## Contributing

Bug reports, clearer documentation, and focused code changes are welcome.

- [CONTRIBUTING.md](CONTRIBUTING.md): how to run tests, sanitizers, and static analysis.
- [AGENTS.md](AGENTS.md): repository conventions.
- [benchmarks/README.md](benchmarks/README.md): repeatable performance checks.
- [include/photoc](include/photoc): shared C APIs and rules for owning and freeing memory.
- [First-release audit](docs/release-audit.md): verification results, remaining limits,
  and checks required before release.
- [Release guide](docs/releasing.md): how to bump a version, publish it, and verify downloads.

Please follow the [code of conduct](CODE_OF_CONDUCT.md). For file corruption,
unsafe overwrites, or suspected vulnerabilities, follow the private reporting
steps in [SECURITY.md](SECURITY.md).

## License

photoc's source is licensed under the [MIT License](LICENSE).
Dependencies retain their own licenses; see [third-party notices](THIRD_PARTY_NOTICES.md).
This software is based in part on the work of the Independent JPEG Group.

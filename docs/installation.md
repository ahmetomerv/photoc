# Installing photoc

[Project README](../README.md) · [Command guides](README.md)

- [Homebrew](#homebrew-recommended): preferred install and upgrade path.
- [GitHub releases](#github-releases): shell installer for a prebuilt executable.
- [Build from source](#build-from-source): any macOS or Linux system with the libraries.
- [Add photoc to your PATH](#add-photoc-to-your-path)
- [Install locations and the manual](#install-locations-and-the-manual)
- [Upgrading](#upgrading)
- [Uninstalling a tracked release installation](#uninstalling-a-tracked-release-installation)
- [Uninstalling a source or manual installation](#uninstalling-a-source-or-manual-installation)

## Homebrew (recommended)

On macOS or Linux with Homebrew, install photoc from its tap:

```sh
brew install ahmetomerv/photoc/photoc
photoc --version
```

The fully qualified name adds the tap automatically and trusts this formula.
Homebrew builds photoc from the tagged source archive and installs its library
dependencies. The tap's formula is updated through a reviewed pull request
after each photoc release; [release-to-tap automation](homebrew-automation.md)
explains that process.

To install a newer version after its tap PR has been merged:

```sh
brew update
brew upgrade photoc
photoc --version
```

`brew update` refreshes the tap definition; `brew upgrade photoc` upgrades
the installed package. `brew update photoc` is not an upgrade command. For a
Homebrew installation, use `brew uninstall photoc` to remove it. If an older
shell or manual copy appears first on `PATH`, run `type -a photoc` to find it
and follow that installation method's uninstall instructions below.

## GitHub releases

The [version-tag workflow](releasing.md) publishes binaries and archives once
all three builds pass. If a release or platform asset is not available,
[build from source](#build-from-source). The installer
reports missing releases or assets as errors; it does not fall back to an
unverified download or build.

This shell installer is an alternative for macOS or Linux systems that use
the published prebuilt executable rather than the Homebrew formula.

Install the runtime libraries first:

```sh
# macOS (Homebrew)
brew install libexif jpeg-turbo libxml2

# Ubuntu / Debian
sudo apt install libexif12 libturbojpeg libjpeg8 libxml2
```

Download the installer, then run it:

```sh
curl -fsSL https://raw.githubusercontent.com/ahmetomerv/photoc/main/scripts/install.sh \
  -o install-photoc.sh
sh install-photoc.sh
```

From a checkout, use `sh scripts/install.sh`. It detects macOS arm64, macOS
x86_64, or Linux x86_64 and installs the latest release's executable to
`$HOME/.local/bin/photoc`. Linux arm64 and other platforms are not supported by
the release installer yet.

Select a release tag or another user-writable location:

```sh
sh scripts/install.sh --version vX.Y.Z
sh scripts/install.sh --install-dir "$HOME/bin"
sh scripts/install.sh --help
```

Replace `vX.Y.Z` with a tag from the
[releases page](https://github.com/ahmetomerv/photoc/releases).

The tag must match a published release. Latest-release resolution is pinned to
one tag before downloading its assets. The install directory must be absolute
and cannot contain `:` because that separates entries in `PATH`.

### Requirements and safety

- Uses `/bin/sh`, `curl`, standard Unix file utilities, and either `sha256sum`
  or `shasum`. No compiler, Python, or automatic `sudo` is needed to install.
- Downloads use HTTPS, including redirects. The binary's SHA-256 digest must
  match exactly one entry in the release's `SHA256SUMS` before it is executed.
- Stages the binary in a private temporary directory, sets executable
  permissions, and checks that `photoc --version` matches the release tag before
  installing it. If the binary
  cannot run (for example, because of missing runtime libraries), installation
  fails with an error. Release-specific runtime requirements belong in the
  release notes; the installer does not install libraries.
  Current release builds target macOS 15+ or glibc-based Linux with glibc 2.35+.
  Runtime packages are `brew install libexif jpeg-turbo libxml2` on macOS, or
  `sudo apt install libexif12 libturbojpeg libjpeg8 libxml2` on Ubuntu.
- Creates the final filename atomically without replacing existing files,
  directories, or symlinks, including entries created during installation.
- Records the installed binary's SHA-256 digest and device/inode identity in
  `.photoc-install-receipt` next to the executable. This private receipt enables
  safe removal; it is never overwritten. To upgrade, back up the binary if
  needed, use the tracked uninstall flow below, then install the new release.
- Cleans temporary downloads and staging files on normal exits and handled
  signals. Errors go to stderr and return a nonzero status: `1` for an
  installation failure or `2` for invalid options.
- Installs the executable and ownership receipt. For the man page and
  completions, use the source installation and
  [completion instructions](../completions/README.md).

If the destination is absent from `PATH`, the installer prints the exact setup
line for bash/zsh. See [Add photoc to your PATH](#add-photoc-to-your-path).

## Build from source

1. Install the build tools and libraries for your system.

   **macOS (Homebrew):** if needed, run `xcode-select --install` to install
   Apple's command-line tools, then run:

   ```sh
   brew install cmake pkgconf libexif jpeg-turbo libxml2
   ```

   **Ubuntu / Debian:**

   ```sh
   sudo apt install build-essential git cmake pkg-config libexif-dev libturbojpeg0-dev libjpeg-dev libxml2-dev
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

If you built from source and prefer to skip installation, replace `photoc` in
the documentation examples with `./build/photoc` while working from the
repository root.

## Add photoc to your PATH

If your shell cannot find `photoc`, add its directory to `PATH`. For the
default `$HOME/.local/bin` location with **bash or zsh**, run this line now and
add it to your shell startup file (`~/.bashrc` or `~/.zshrc`) for future
sessions:

```sh
export PATH="$HOME/.local/bin:$PATH"
```

Use your chosen directory instead if you installed somewhere else. Check which
copy your shell runs with `command -v photoc`.

## Install locations and the manual

A source installation puts files in these locations under your install prefix:

- Executable: `bin/photoc`
- Man page: `share/man/man1/photoc.1`
- License notices: `share/doc/photoc`

To change these locations, configure `CMAKE_INSTALL_BINDIR`,
`CMAKE_INSTALL_MANDIR`, or `CMAKE_INSTALL_DOCDIR` when building.

Read the [man page](../man/photoc.1) with `man photoc`. If your shell cannot
find the installed manual, use `man -M "$HOME/.local/share/man" photoc`. From a
checkout, use `man ./man/photoc.1`. The release installer does not install the
man page.

Shell completions for zsh, bash, and fish are installed separately; see the
[completion instructions](../completions/README.md).

## Upgrading

Installed copies do not update automatically. Read the
[release notes](https://github.com/ahmetomerv/photoc/releases) and check your
current version with `photoc --version` before upgrading.

For Homebrew installations, use `brew update` followed by
`brew upgrade photoc` as shown [above](#homebrew-recommended). Wait until the
tap PR for the new version has been merged. The commands below apply to the
shell installer, which does not overwrite an existing executable.

From a checkout, run:

```sh
# 1. Preview removal of your current tracked installation.
sh scripts/uninstall.sh

# 2. After reviewing the preview, remove it.
sh scripts/uninstall.sh --apply

# 3. Install the latest published release and check its version.
sh scripts/install.sh
"$HOME/.local/bin/photoc" --version
```

Without a checkout, download the installer as described above and the
uninstaller as described below. Use `sh uninstall-photoc.sh`,
`sh uninstall-photoc.sh --apply`, and `sh install-photoc.sh` in the same order.

For a custom directory, pass the same `--install-dir` to every install and
uninstall command, then run the executable from that directory to verify it.
The installer will not overwrite an existing copy. If the uninstaller reports
an installation without a receipt, follow the
[source/manual uninstall steps](#uninstalling-a-source-or-manual-installation)
before proceeding. If a tracked release's executable was modified, review that
change before removing anything; the receipt no longer verifies it.

You can select a version by adding `--version vX.Y.Z` to the install command.
To return to an earlier release, uninstall the current tracked copy and install
that earlier tag. Keep any backup you need before uninstalling: the upgrade
steps remove the old executable before downloading the new one. A failed
download or a missing runtime library leaves photoc uninstalled until you
successfully install a release again. Photos and shell configuration are kept.

Source installations use a different update process. From your checkout,
fetch the new release, then rebuild, test, and reinstall with your original
prefix:

```sh
git fetch --tags
git checkout vX.Y.Z
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$HOME/.local"
```

If you change CMake options or dependencies, rerun the configure command from
[Build from source](#build-from-source) first. See
[Updating after local changes](../CONTRIBUTING.md#updating-after-local-changes)
if you are building your own edits.

## Uninstalling a tracked release installation

From a checkout, preview first, then explicitly apply removal:

```sh
sh scripts/uninstall.sh
sh scripts/uninstall.sh --apply

# Use the directory originally passed to install.sh:
sh scripts/uninstall.sh --install-dir "$HOME/bin" --apply
```

Without a checkout, download the script:

```sh
curl -fsSL https://raw.githubusercontent.com/ahmetomerv/photoc/main/scripts/uninstall.sh \
  -o uninstall-photoc.sh
sh uninstall-photoc.sh
sh uninstall-photoc.sh --apply
```

The default is a dry run (`--dry-run` is also accepted). `--apply` removes only
the unchanged `photoc` executable recorded by `scripts/install.sh` and its
`.photoc-install-receipt`. Both the checksum and file identity must match.
Replacing, editing, or symlinking the binary blocks removal; a malformed or
symlinked receipt also blocks it. If the recorded binary is already absent,
only its valid receipt is removed. Repeated uninstall of an empty location is
successful and creates no directories.

Configuration, photos, unrelated executables, shell startup/PATH settings,
completions, man pages, and the installation directory are preserved. The
script runs locally, does not execute the installed binary, and never uses
`sudo`. It requires standard Unix utilities plus `sha256sum` or `shasum` on
macOS or Linux. Exit statuses are `0` for a successful preview/removal, `1` for
an operational failure, and `2` for invalid options.

During removal, files are moved into a private temporary directory and verified
again before deletion. If a concurrent change or filesystem error prevents
safe completion, remaining files are preserved and their location is printed
to stderr. Restore those files to their original locations manually before
retrying; the script does not overwrite a replacement file during recovery.

**CMake, manual, and earlier installations without this receipt are untracked.**
The script refuses to remove them. Follow the steps below instead. The receipt
does not authorize deleting a file that was replaced by a package manager or
another installation.

## Uninstalling a source or manual installation

An installation made with `cmake --install` has no `.photoc-install-receipt`.
Running either `sh scripts/uninstall.sh` or a downloaded `uninstall-photoc.sh`
therefore reports `no regular ownership receipt` and removes nothing. Both are
the same release uninstaller; downloading it again will not add the missing
receipt.

For a source installation:

1. Check the executable your shell finds:

   ```sh
   command -v photoc
   ```

2. From the source checkout, review the install manifest in the build directory
   you used:

   ```sh
   cat build/install_manifest.txt
   ```

   If you used another build directory, change `build` in this path. The
   manifest records that build tree's most recent installation; check that
   its paths match the installation you want to remove. If it is missing,
   review your original install prefix and CMake layout instead.

3. For the `cmake --install build --prefix "$HOME/.local"` example in
   [Build from source](#build-from-source),
   confirm the executable is `$HOME/.local/bin/photoc`, then remove it:

   ```sh
   rm "$HOME/.local/bin/photoc"
   ```

   For a custom prefix or `CMAKE_INSTALL_BINDIR`, use the actual installed path.
   Remove it only if it still belongs to this installation. If a package
   manager or another installer replaced it, use that installation's removal
   method.

4. For a full uninstall, review the other files in the manifest and remove
   only the files belonging to this source installation. The default man page
   can be removed with:

   ```sh
   rm "$HOME/.local/share/man/man1/photoc.1"
   ```

   Run this only if the manifest and your chosen layout confirm that path.
   License notices normally live under `$HOME/.local/share/doc/photoc`;
   review the manifest before removing those individual files. Keep directories
   that contain unrelated files, and keep completion/PATH settings if you plan
   to install another version.

These steps remove installed files, leaving the source checkout and build
directory available. Running `./build/photoc` without `cmake --install` creates
no installed copy. For a manually copied executable, check `command -v photoc`
and remove only the copy you placed there; there may be no CMake manifest.

## Release asset contract

The release workflow publishes these **direct executables** for the installer:

| Platform | Asset |
| --- | --- |
| macOS arm64 | `photoc-darwin-arm64` |
| macOS x86_64 | `photoc-darwin-x86_64` |
| Linux x86_64 | `photoc-linux-x86_64` |

Each binary must run on its advertised target and support `--version`. Publish
`SHA256SUMS` alongside them, using `sha256sum` or `shasum -a 256` format:

```sh
shasum -a 256 photoc-darwin-arm64 photoc-darwin-x86_64 photoc-linux-x86_64 > SHA256SUMS
```

The installer downloads both assets from
`https://github.com/ahmetomerv/photoc/releases/download/<tag>/`. It expects a
64-digit hexadecimal digest followed by the exact asset filename. Checksums
detect download corruption; the manifest is fetched from the same release,
not an independent signature. The release workflow also attaches versioned
`photoc-<version>-<platform>.tar.gz` archives and individual platform checksum
manifests. See [releasing.md](releasing.md) for the complete layout and workflow.

Sony ARW support is metadata-only and uses the existing
libexif dependency; no RAW development library is required. See
[the command/file-type support matrix](raw.md#supported-commands).

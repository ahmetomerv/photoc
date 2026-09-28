# Installing photoc

## GitHub releases

The [version-tag workflow](releasing.md) publishes binaries and archives once
all three builds pass. If a release or platform asset is not available,
[build from source](../README.md#build-from-source). The installer
reports missing releases or assets as errors; it does not fall back to an
unverified download or build.

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
sh scripts/install.sh --version v0.1.0
sh scripts/install.sh --install-dir "$HOME/bin"
sh scripts/install.sh --help
```

The tag must match a published release. Latest-release resolution is pinned to
one tag before downloading its assets. The install directory must be absolute
and cannot contain `:` because that separates entries in `PATH`.

### Requirements and safety

- Uses `/bin/sh`, `curl`, standard Unix file utilities, and either `sha256sum`
  or `shasum`. No compiler, Python, or automatic `sudo` is needed to install.
- Downloads use HTTPS, including redirects. The binary's SHA-256 digest must
  match exactly one entry in the release's `SHA256SUMS` before it is executed.
- Stages the binary in a private temporary directory, sets executable
  permissions, and checks `photoc --version` before installing it. If the binary
  cannot run (for example, because of missing runtime libraries), installation
  fails with an error. Release-specific runtime requirements belong in the
  release notes; the installer does not install libraries.
  Current release builds target macOS 15+ or glibc-based Linux with glibc 2.35+.
  Runtime packages are `brew install libexif jpeg-turbo` on macOS, or
  `sudo apt install libexif12 libturbojpeg` on Ubuntu.
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
line for bash/zsh and fish. For the default location:

```sh
# bash/zsh: add to ~/.bashrc or ~/.zshrc, then open a new shell
export PATH="$HOME/.local/bin:$PATH"

# fish
fish_add_path "$HOME/.local/bin"
```

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
The script refuses to remove them. For a source install, review the corresponding
`build/install_manifest.txt` and the current files before manually removing
any entries. The receipt does not authorize deleting a file that was replaced
by a package manager or another installation.

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

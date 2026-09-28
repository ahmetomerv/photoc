# Installing photoc

## GitHub releases

**Release binaries are not published yet.** Until the assets described below are
available, [build from source](../README.md#build-from-source). The installer
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
- Creates the final filename atomically without replacing existing files,
  directories, or symlinks, including entries created during installation.
  To upgrade, move your current `photoc` aside first and keep it until the new
  installation succeeds.
- Cleans temporary downloads and staging files on normal exits and handled
  signals. Errors go to stderr and return a nonzero status: `1` for an
  installation failure or `2` for invalid options.
- Installs only the executable. For the man page and completions, use the
  source installation and [completion instructions](../completions/README.md).

If the destination is absent from `PATH`, the installer prints the exact setup
line for bash/zsh and fish. For the default location:

```sh
# bash/zsh: add to ~/.bashrc or ~/.zshrc, then open a new shell
export PATH="$HOME/.local/bin:$PATH"

# fish
fish_add_path "$HOME/.local/bin"
```

## Release asset contract

Publish these **direct executables**, not archives, as assets on a release:

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
not an independent signature. Release publishing is not automated by this
installer or CI.

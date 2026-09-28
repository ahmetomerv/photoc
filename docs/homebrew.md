# Homebrew tap

photoc provides a [formula template](../packaging/homebrew/photoc.rb.in) for an
independent tap. It builds from a checksum-pinned GitHub **tagged source archive**
on macOS or Linux. It declares `cmake` and `pkgconf` as build dependencies and
`libexif` and `jpeg-turbo` as runtime dependencies. CMake installs the binary and
man page; the formula installs Bash, zsh, and fish completions into Homebrew's
standard directories.

This prepares tap distribution. No tap is published by this repository, and
there is no homebrew-core submission or acceptance. A public version tag is
required before generating a usable formula; a placeholder checksum is never
published.

## Install from a published tap

Replace `<owner>` with the GitHub owner of the published `homebrew-tap` repository:

```text
brew install <owner>/tap/photoc
```

For example, after `ahmetomerv/homebrew-tap` has been published:

```sh
brew install ahmetomerv/tap/photoc
photoc --version
man photoc
```

Homebrew adds the tap automatically. Alternatively, run `brew tap <owner>/tap`
first. This formula builds from source and requires the platform's compiler
toolchain; no bottles are supplied here. Homebrew manages dependency installation,
upgrades, and removal:

```sh
brew update
brew upgrade ahmetomerv/tap/photoc
brew uninstall ahmetomerv/tap/photoc
```

Use Homebrew to remove its installation; `scripts/uninstall.sh` applies to the
separate release installer. For shell initialization, see Homebrew's
[completion setup](https://docs.brew.sh/Shell-Completion).

## Create and publish the tap

1. Follow the [release steps](../CONTRIBUTING.md#releases) to publish a version tag
   containing the current source layout. Work from a checkout of that tag.
2. Download its source archive and generate the formula from the canonical
   `VERSION` and the archive's real SHA-256 digest:

   ```sh
   version=$(python3 scripts/package-release.py version)
   mkdir -p dist/homebrew
   curl --fail --location --proto '=https' --proto-redir '=https' \
     "https://github.com/ahmetomerv/photoc/archive/refs/tags/v$version.tar.gz" \
     -o "dist/homebrew/photoc-$version-source.tar.gz"
   python3 scripts/package-release.py homebrew \
     --source-archive "dist/homebrew/photoc-$version-source.tar.gz" \
     --output dist/homebrew/photoc.rb
   ```

   The generator checks the archive's version and required source files, and
   refuses to replace an existing output. Use a fresh output path for updates.
   Python is a maintainer tool; it is not a formula build or runtime dependency.
   The generated formula is standalone: it does not need this checkout in the tap.
3. Create a local tap, replacing the owner value, and copy the generated formula:

   ```sh
   owner=YOUR_GITHUB_USERNAME
   brew tap-new "$owner/tap"
   tap_dir=$(brew --repository "$owner/tap")
   cp dist/homebrew/photoc.rb "$tap_dir/Formula/photoc.rb"
   brew style "$owner/tap/photoc"
   brew audit --strict "$owner/tap/photoc"
   brew install --build-from-source "$owner/tap/photoc"
   brew test "$owner/tap/photoc"
   ```

   The formula test checks the version, man page, and all three completion files.
   Test on macOS and Linux before advertising both platforms.
   `tap-new` also generates optional bottle workflows. Review them before
   publishing, or omit them from the tap for source-only installation.
4. Create an empty public GitHub repository named `homebrew-tap` under that owner,
   add a short README with installation instructions, then commit and push:

   ```sh
   git -C "$tap_dir" add Formula/photoc.rb README.md
   git -C "$tap_dir" commit -m "Add photoc formula"
   git -C "$tap_dir" remote add origin "https://github.com/$owner/homebrew-tap.git"
   git -C "$tap_dir" push -u origin HEAD
   ```

For each later release, check out its tag, regenerate the formula, review the
new URL/checksum, copy it into the tap, repeat the checks, and commit the update.
Do not hand-maintain a second version field. Homebrew derives the formula
version from the tagged URL; the build verifies it against the source `VERSION`.

The project's release workflow continues to publish GitHub assets only; it does
not push formula changes or bottles. See Homebrew's
[tap guide](https://docs.brew.sh/How-to-Create-and-Maintain-a-Tap) and
[formula cookbook](https://docs.brew.sh/Formula-Cookbook).

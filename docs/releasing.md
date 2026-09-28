# Releasing photoc

The [release workflow](../.github/workflows/release.yml) runs when a tag matching
`v*` is pushed. It accepts stable tags in the form **`vMAJOR.MINOR.PATCH`**,
without leading zeroes. The tag must be `v` followed by the exact value in the
tagged commit's [`VERSION`](../VERSION) file. Prerelease tags are rejected for now.

## Prepare the version

Run these steps from a clean checkout of `main`, after reviewing the changes
you want to release. [`VERSION`](../VERSION) is the single source for the
version number; CMake generates the CLI version header from it.

For the first release, keep the existing **0.1.0**. For later releases, choose
a bump using the [version policy](../CONTRIBUTING.md#version-policy):

```sh
# Preview the next version without changing any files.
python3 scripts/package-release.py bump patch

# Apply the chosen bump to VERSION.
python3 scripts/package-release.py bump patch --apply
```

Use `minor` or `major` instead of `patch` when appropriate. From `0.1.0`,
the results are `0.1.1`, `0.2.0`, and `1.0.0`, respectively. Minor and major
bumps reset the lower numbers to zero. The helper changes only `VERSION`;
it does not commit, tag, or publish anything.

Update [docs/release-notes.md](release-notes.md) with the changes in this
version, any breaking changes, and current runtime requirements. The workflow
copies this file into the GitHub release body and adds generated GitHub notes
and license texts. Published release notes remain attached to their own tags.
Review [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) and the included
license texts against the dependency versions used by the release builds.

## Publish a version

1. Build and test the prepared version:

   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ctest --test-dir build --output-on-failure
   ./build/photoc --version
   ```

2. Commit and push the reviewed changes, version file, and release notes to
   `main`. Include any other changed files intended for the release:

   ```sh
   version=$(python3 scripts/package-release.py version)
   git add VERSION docs/release-notes.md
   git commit -m "chore: prepare release $version"
   git push origin main
   ```

3. Wait for **CI on that exact commit** to pass, including macOS, Ubuntu,
   formatting, and the Linux sanitizer job. Find its run with:

   ```sh
   gh run list --workflow ci.yml --branch main --commit "$(git rev-parse HEAD)"
   ```

4. Once CI passes, create and push the annotated tag:

   ```sh
   version=$(python3 scripts/package-release.py version)
   git tag -a "v$version" -m "photoc $version"
   git push origin "v$version"
   ```

5. Watch the [Release workflow](https://github.com/ahmetomerv/photoc/actions/workflows/release.yml).
   It checks the tag against `VERSION` and requires passing main-branch CI for
   the tagged commit. All three native builds compile with warnings enabled
   and run CTest before packaging. Packaging also checks the executable version.
   The publish job verifies checksums, uploads all assets, and publishes the
   draft only when those steps succeed.

6. Open the release and verify its notes and downloads. A successful release
   has three executables, three archives, three platform checksum files, and
   one combined `SHA256SUMS`. Check the real installer in a temporary directory:

   ```sh
   release_test_dir=$(mktemp -d)
   sh scripts/install.sh --version "v$version" --install-dir "$release_test_dir"
   "$release_test_dir/photoc" --version
   sh scripts/uninstall.sh --install-dir "$release_test_dir"
   sh scripts/uninstall.sh --install-dir "$release_test_dir" --apply
   rmdir "$release_test_dir"
   ```

   Repeat this on each supported platform when available. The version
   badge reads published GitHub releases and will update after publication;
   cached badges may take a little time to refresh.

Use this same process for every future release. Never move a published tag or
replace published assets; fixes need a new version. Publishing makes the
default installer select the new release. Existing installations update only
when users follow the [upgrade steps](installation.md#upgrading).

| Target | Native runner | Binary baseline |
| --- | --- | --- |
| macOS arm64 | `macos-15` | macOS 15 or newer |
| macOS x86_64 | `macos-15-intel` | macOS 15 or newer |
| Linux x86_64 | `ubuntu-22.04` | glibc 2.35 or newer |

The runner architecture is checked before building. macOS builds set an
explicit architecture and macOS 15 deployment target. Dependencies are installed
with the system package manager and remain dynamically linked. Runtime installation
commands and limits are included in the release notes.

## Assets

For tag `v<version>`, each platform produces:

- `photoc-<version>-<platform>.tar.gz`, where `<platform>` is `darwin-arm64`,
  `darwin-x86_64`, or `linux-x86_64`.
- `photoc-<platform>`, the raw executable consumed by `scripts/install.sh`.
- `SHA256SUMS-<platform>`, covering both the archive and raw executable.

The publish job verifies all six files against their platform manifests and
generates **`SHA256SUMS`** with all six digests, sorted by filename. Archives,
executables, individual manifests, and the combined manifest are attached to
the GitHub release. Asset names agree with the
[release installer contract](installation.md#release-asset-contract).

Each archive has one `photoc-<version>-<platform>/` directory containing:

- `bin/photoc` and `share/man/man1/photoc.1`.
- `completions/`, `docs/`, README with its `assets/`, license, and contributor guidance.
- `THIRD_PARTY_NOTICES.md` and `licenses/` with dependency license texts.
- `VERSION`, recording the packaged version.
- `scripts/install.sh` and `scripts/uninstall.sh`.

Archives preserve executable permissions and use normalized timestamps and
ownership. This makes packaging repeatable for identical input files; native
builds and changing system dependencies are not promised to be reproducible.
The release body includes the dependency notices for raw executable downloads.
Review these against the installed dependency versions before each release.

## Publication and retries

Build jobs have read-only repository permissions. Only the publish job has
`contents: write`, using the workflow's `GITHUB_TOKEN`; no personal token is
needed. It runs after all builds succeed, creates a draft, uploads assets, and
publishes the draft after successful upload. A failed upload leaves a draft
that can be retried. On retry, only a draft's matching assets can be replaced.
An already published release is refused. Per-tag concurrency prevents two
runs for the same tag from publishing at once. If validation ran before CI
finished, wait for CI to pass and rerun the failed Release workflow on the same
tag. For a build or upload failure, inspect the job logs and rerun after resolving
the cause. If the source itself needs a fix, prepare a new version and tag.

Do not move tags associated with published releases. Keep release publishing
limited to trusted maintainers who can push version tags. This workflow does
not sign/notarize macOS binaries.

## Local packaging check

After building and testing the native executable:

```sh
version=$(python3 scripts/package-release.py version)
python3 scripts/package-release.py package --tag "v$version" \
  --platform darwin-arm64 --binary build/photoc --output dist
python3 tests/test_release_packaging.py
```

Select the platform matching the build; the packaging tool does not cross-compile.
It refuses to overwrite existing output files. Once all three sets of assets
are in the same directory, verify and combine their checksums with:

```sh
python3 scripts/package-release.py checksums --tag "v$version" --directory dist
```

Packaging and checksum generation use only Python's standard library. CTest
includes packaging tests when Python 3 is available. GitHub-hosted builds and
actual release publication are exercised by pushing a version tag.

# Releasing photoc

The [release workflow](../.github/workflows/release.yml) runs when a tag matching
`v*` is pushed. It accepts stable tags in the form **`vMAJOR.MINOR.PATCH`**,
without leading zeroes. The tag must be `v` followed by the exact value in the
tagged commit's [`VERSION`](../VERSION) file. Prerelease tags are rejected for now.

## Publish a version

Follow the [version policy and release steps in CONTRIBUTING.md](../CONTRIBUTING.md#releases).
Update only `VERSION`; CMake generates the version header used by the CLI.
The workflow validates the tag against this source before building and
publishing. Packaging also verifies that the compiled `photoc --version` agrees.
All three native builds configure CMake in Release mode, build with warnings
enabled, and run CTest before packaging.

| Target | Native runner | Binary baseline |
| --- | --- | --- |
| macOS arm64 | `macos-15` | macOS 15 or newer |
| macOS x86_64 | `macos-15-intel` | macOS 15 or newer |
| Linux x86_64 | `ubuntu-22.04` | glibc 2.35 or newer |

The runner architecture is checked before building. macOS builds set an
explicit architecture and macOS 15 deployment target. Dependencies are installed
with Homebrew or apt and remain dynamically linked. Runtime installation
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
- `completions/`, `docs/`, README, license, and contributor guidance.
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
runs for the same tag from publishing at once.

Do not move tags associated with published releases. Keep release publishing
limited to trusted maintainers who can push version tags. This workflow does
not sign/notarize macOS binaries or publish a Homebrew formula.

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

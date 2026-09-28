# Contributing to photoc

Thanks for helping build `photoc`.

1. Keep changes focused and avoid adding dependencies without a clear need.
2. Use C17 and follow the existing source layout: routing in `src/core/`, command implementations in `src/commands/`, and tests in `tests/`.
3. Build and run the tests before submitting a change:

   ```sh
   sh scripts/build-and-test.sh
   ```

   The normal build already treats compiler warnings as errors
   (`-Wall -Wextra -Wpedantic`, plus format, shadow, switch, prototype,
   and a few other defect checks). GCC and Clang are both fine. MSVC uses
   `/W4 /WX`.

4. Run static analysis before submitting a change that touches C sources:

   ```sh
   sh scripts/static-analysis.sh
   ```

   That configures `build-analyze` and runs `clang --analyze` on every
   project C file, using the CMake compilation database. Annex K
   `fprintf_s` warnings are disabled because those functions are not part
   of the portable C17 surface this project uses.

   `scan-build` is the same analyzer with a compiler wrapper. Configure a
   fresh directory so CMake records the wrapper as the compiler:

   ```sh
   scan-build cmake -S . -B build-scan
   scan-build --status-bugs \
     -disable-checker security.insecureAPI.DeprecatedOrUnsafeBufferHandling \
     cmake --build build-scan
   ```

   clang-tidy runs in the same script when `clang-tidy` is on `PATH`.
   Checks live in `.clang-tidy` and stay on defect findings rather than
   style. Install it with one of:

   ```sh
   # macOS (Homebrew LLVM; Apple Clang does not ship clang-tidy)
   brew install llvm
   export PATH="$(brew --prefix llvm)/bin:$PATH"

   # Debian/Ubuntu
   sudo apt install clang-tidy

   # Fedora
   sudo dnf install clang-tools-extra
   ```

   To make a build fail on those same clang-tidy checks:

   ```sh
   cmake -S . -B build-tidy -DPHOTOC_CLANG_TIDY=ON
   cmake --build build-tidy
   ```

   `CMAKE_EXPORT_COMPILE_COMMANDS` is on, so
   `clang-tidy -p build-analyze src/core/fs.c` also works after the script
   has configured `build-analyze`.

5. Add or update tests when adding behavior. Keep fixtures small and place them
   in `tests/fixtures/`. Prefer generated synthetic images over external photos;
   see [`tests/fixtures/README.md`](tests/fixtures/README.md). Regenerate binary
   fixtures with `python3 scripts/make-fixtures.py` when you change a generator.
6. Optional: for scan-heavy changes, run `python3 scripts/benchmark.py` and note
   any large shift versus [`benchmarks/README.md`](benchmarks/README.md). Do not
   chase micro-optimizations without a measured regression.
7. For concurrency changes, use a separate ThreadSanitizer build when supported:

   ```sh
   cmake -S . -B build-tsan -DPHOTOC_THREAD_SANITIZER=ON
   cmake --build build-tsan
   ctest --test-dir build-tsan --output-on-failure
   ```

   This option is separate from `PHOTOC_SANITIZERS`; ThreadSanitizer and
   AddressSanitizer cannot be enabled together. Reproducible worker-count
   benchmarks are documented in [`benchmarks/README.md`](benchmarks/README.md).
8. Describe what changed and how it was tested in your pull request.

## Code formatting

Use **clang-format 18** and the repository's [`.clang-format`](.clang-format):
four-space indentation, an 80-column limit, function braces on their own line,
and attached braces for control statements. Include order and comment text are
preserved. The formatter is a development tool, not a runtime dependency.

Install it with:

```sh
# Debian/Ubuntu
sudo apt install clang-format-18

# macOS
brew install llvm@18
export CLANG_FORMAT="$(brew --prefix llvm@18)/bin/clang-format"
```

From the repository root, format project C sources and headers before submitting:

```sh
sh scripts/format.sh --write
```

Run the same read-only check as CI:

```sh
sh scripts/format.sh --check
```

The script defaults to `--check`, prefers `clang-format-18` on `PATH`, and accepts
`CLANG_FORMAT` to select the version 18 executable. It covers `.c` and `.h` files
under `src/`, `include/`, `tests/`, and `benchmarks/`. It excludes fixture,
generated, vendor, and `third_party` directories; build outputs and other file
types are outside its scope. Do not reformat generated or vendored files.

## Continuous integration

[GitHub Actions CI](.github/workflows/ci.yml) runs on pushes, pull requests,
and manual dispatches. Release builds configure CMake, compile with warnings
enabled, and run CTest on Ubuntu 24.04 and macOS 15. A separate Ubuntu Clang
Debug build enables AddressSanitizer and UndefinedBehaviorSanitizer, including
leak detection on Linux. A separate formatting job uses clang-format 18 and
fails when project C sources or headers need formatting.

CI installs libexif, TurboJPEG, and native test tools so completion and man-page
checks run alongside the C and CLI tests. It does not cache dependencies or
publish releases. The sanitizer build can be reproduced locally with
`sh scripts/build-and-test-sanitizers.sh`; leak detection depends on platform
support.

## Install and uninstall script checks

The release installer is POSIX shell and installs only checksum-verified release
binaries. Its asset contract is in [installation documentation](docs/installation.md).
Before changing it, run:

```sh
sh -n scripts/install.sh
sh -n scripts/uninstall.sh
shellcheck --shell=sh scripts/install.sh scripts/uninstall.sh
python3 tests/test_installer.py
python3 tests/test_uninstaller.py
```

The Python test uses only the standard library, mocks GitHub downloads and
platform detection, and installs into temporary directories. It covers all
supported platforms, checksum and download failures, PATH guidance, cleanup,
and preservation of existing files. Uninstall tests also cover the receipt,
file identity checks, previews, and preservation of modified or untracked
files and user configuration. CTest runs both suites when Python 3 is available;
Ubuntu CI also runs ShellCheck. Neither tool is a runtime dependency.

## Releases

### Version policy

photoc uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
[`VERSION`](VERSION) is the only manually maintained version source: one
`MAJOR.MINOR.PATCH` value, without a `v` prefix or leading zeroes. Stable versions
are supported; prerelease and build metadata suffixes are not supported yet.

The public interface includes documented commands, options, exit codes, and
JSON schemas. After 1.0, increment **major** for incompatible interface changes,
**minor** for compatible additions, and **patch** for compatible fixes; reset
lower components to zero when increasing major or minor. During `0.x` development,
the interface can change: use a minor bump for features or breaking changes and
a patch bump for compatible fixes. Describe breaking changes in release notes.

CMake reads `VERSION`, sets `PROJECT_VERSION`, and generates the header used by
`photoc --version`. Editing `VERSION` also triggers CMake regeneration on the
next build. Do not edit generated headers, add version literals to C sources,
or derive the application version from Git state. Release tooling reads the
same file and rejects tags or binaries that disagree with it.

### Publish a release

1. Commit and review the intended changes and release notes, then update
   **only `VERSION`** to set the next version.
2. Configure, build, and run all tests from the repository root:

   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ctest --test-dir build --output-on-failure
   ./build/photoc --version
   ```

3. Commit the version change, then create and push an annotated tag whose name
   is `v` followed by the canonical version:

   ```sh
   version=$(python3 scripts/package-release.py version)
   git add VERSION
   git commit -m "chore: release $version"
   git tag -a "v$version" -m "photoc $version"
   git push origin HEAD
   git push origin "v$version"
   ```

4. Watch the Release workflow. It validates the tag against `VERSION`, builds
   and tests native macOS arm64, macOS x86_64, and Linux x86_64 binaries, checks
   their versions, and uploads verified assets before publishing the draft.
   Inspect the published assets and notes. Never move a published tag or replace
   published assets; fixes require a new version.

See [docs/releasing.md](docs/releasing.md) for asset names, runtime requirements,
and retry behavior. Packaging and versioning checks use Python's standard
library and run through CTest when Python 3 is available, or directly with:

```sh
python3 tests/test_release_packaging.py
python3 tests/test_versioning.py
```

The normal CI workflow does not publish releases; the separate Release
workflow grants write permission only to its publish job. Homebrew publication
is not configured.

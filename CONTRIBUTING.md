# Contributing to photoc

Thanks for helping build `photoc`.

Follow the [code of conduct](CODE_OF_CONDUCT.md). Use the bug/feature templates
for public issues and the pull request template to describe changes and checks.
Report suspected vulnerabilities or data-safety defects through
[SECURITY.md](SECURITY.md) before sharing details publicly.

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
6. Optional: for scan- or image-heavy changes, run `python3 scripts/benchmark.py`
   and `python3 scripts/benchmark-image.py` and note any large shift versus
   [`benchmarks/README.md`](benchmarks/README.md). The image benchmarks need
   `cjpeg` from libjpeg-turbo. Do not chase micro-optimizations without a
   measured regression.
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

## Pull requests and checks

Work on a branch based on the latest `main`, then push the branch and open a
pull request targeting `main`. Keep the pull request focused and update the same
branch as you address feedback. Before merging, check the formatting, Ubuntu
and macOS build/test, and Ubuntu sanitizer jobs on the pull request. Review
CodeQL and CodeRabbit findings when those checks appear, and resolve failures
or actionable findings. GitHub's branch ruleset determines which checks are
required for merging.

## Ways to contribute without writing C

- **Report bugs and confusing behavior** with the bug report template. Include
  the command, your OS, `photoc --version`, and the full stderr output.
- **Improve documentation.** Unclear wording, missing examples, and broken
  links are all welcome fixes.
- **Share sample files from other cameras.** Support for a new camera or RAW
  format depends on real metadata layouts. Open a feature request naming the
  camera model and file type, and describe a small sample you can share. Only
  share files you own and are happy to publish, and remove GPS or other
  private details first (for JPEGs, `photoc scrub --privacy` can help).
  Test fixtures are small generated files rather than camera photographs, so
  samples guide the fixture generators instead of being committed as-is; see
  [`tests/fixtures/README.md`](tests/fixtures/README.md).

## Updating after local changes

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
[Build from source](docs/installation.md#build-from-source), including your
chosen options, before rebuilding.

## Project references

- [AGENTS.md](AGENTS.md): repository conventions.
- [include/photoc](include/photoc): shared C APIs and rules for owning and freeing memory.
- [benchmarks/README.md](benchmarks/README.md): repeatable performance checks.
- [First-release audit](docs/release-audit.md): verification results, remaining
  limits, and checks required before release.
- [Release guide](docs/releasing.md): how to bump a version, publish it, and
  verify downloads.

## Metadata formats

`include/photoc/format.h` separates extension discovery from backend validation.
The generic `Photo` model includes format, orientation, and presence flags.
`src/core/metadata.c` dispatches JPEG and Sony ARW loading, while
`metadata_internal.h` shares libexif field conversions. `arw.c` selects common
entries through the bounded read-only `tiff.c` directory visitor; no image,
MakerNote, or private codec parser is invoked. See [ARW support](docs/raw.md)
for the dependency evaluation, limits, and test provenance.

The default metadata scanner discovers JPEG/ARW. Consumers needing JPEG alone
must request `PHOTOC_FORMATS_JPEG` via `photoc_scan_directory_filtered` (query)
or keep their JPEG filesystem/image gates (check/compress/focus/scrub).
Rename/sort share format discovery but retain their existing plans, preflight,
no-overwrite apply, and rollback code. Future metadata backends must not expand
image-operation support merely by changing discovery.

## Statistics aggregation

`src/core/stats.c` owns buckets and scalar size/timestamp samples; it retains no
Photo pointers or pixels. Missing values use presence flags and remain excluded.
Finalization sorts distributions/samples and uses `photoc_session_starts_new`
from the existing session core for summaries. Standard EXIF LensModel and
FocalLengthIn35mmFilm are read by both metadata backends without MakerNotes or
crop-factor inference. Command formatting appends sections/JSON fields while
preserving existing distributions, percentages, and field types.

## Timeline aggregation

`src/core/timeline.c` retains owned compact metadata copies from borrowed
scanner Photos, validates capture timestamps through the shared timestamp
API, and sorts them before grouping by date. Within a date it reuses
`photoc_session_starts_new`; midnight starts another session. Per-session
exposure/camera/file-size aggregation reuses core stats with only the needed
Photo fields. Cleanup owns all strings, arrays, buckets, and samples; no
pixels or source Photo pointers are retained. `src/commands/timeline.c` handles
scan warnings and human/JSON output through existing helpers. See
[timeline definitions](docs/timeline.md) for clock and skip semantics.

## Metadata query architecture

`src/core/query.c` parses numeric comparisons, validates inclusive date bounds
using the shared timestamp API, and evaluates predicates against `Photo`
presence flags. `include/photoc/query.h` exposes a compiled query that borrows
filter strings and owns no allocations. The shared parser is linked into
`photoc_core` so comparator parsing can reuse its numeric grammar.

`src/commands/query.c` reuses the bounded photo scanner and retains owned copies
of matching metadata only, then sorts paths before writing line, NUL, or JSON
output. Predicate logic performs no I/O and knows nothing about formatting.
A skipped JPEG load fails the search with status 1 while retaining other
matches; the scanner's existing treatment of unavailable EXIF is unchanged.
Path modes write only requested paths to stdout, regardless of verbosity.

## JPEG audit architecture

`src/core/jpeg_check.c` owns the read-only audit and stable status/reason model
in `include/photoc/jpeg_check.h`. It reuses the JPEG marker parser and the
shared EXIF loader; audit-only TIFF bounds/log checks do not change editing
rules. Command collection, sorted reports, and CLI options live in
`src/commands/check.c` and the existing CLI/parser tables. JSON strings and
verbosity use the shared helpers.

The audit uses the streaming libjpeg interface from the existing libjpeg-turbo
dependency because TurboJPEG requires a complete compressed buffer and its
current wrapper collapses warnings into failures. Link the additional `libjpeg`
shared library from that same dependency (`libjpeg-dev` for Ubuntu builds,
`libjpeg8` for runtime). No new third-party project or copied implementation is
introduced. Fatal decoder recovery uses heap-owned state and `setjmp`/`longjmp`
so cleanup cannot depend on modified automatic variables. No decoded image is
retained; files are checked serially to bound multi-scan memory. The photo
metadata scanner intentionally is not used to discard unreadable candidates.

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

[GitHub Actions CI](.github/workflows/ci.yml) runs on pull requests, pushes to
`main`, and manual dispatches. A push to a feature branch with an open pull
request does not start a duplicate CI run. The `main` push run checks the merged
commit; the release workflow requires that run to pass for the tagged commit.
CI builds configure CMake, compile with warnings enabled, and run CTest on
Ubuntu 24.04 and macOS 15. A separate Ubuntu Clang Debug build enables
AddressSanitizer and UndefinedBehaviorSanitizer, including
leak detection on Linux. A separate formatting job uses clang-format 18 and
fails when project C sources or headers need formatting.

CI installs libexif, TurboJPEG, libxml2, and native test tools so completion and man-page
checks run alongside the C and CLI tests. It also runs both benchmark scripts
with a tiny synthetic corpus (`benchmark.py` and `benchmark-image.py`, the
latter via `cjpeg` from libjpeg-turbo) to keep the harness working; it does not
track benchmark timings. CI does not cache dependencies or
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
[`VERSION`](VERSION) is the single version source: one
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

Follow the numbered [release guide](docs/releasing.md) for the complete process:

1. Keep `0.1.0` for the first release. For later releases, preview a bump with
   `python3 scripts/package-release.py bump patch`, then add `--apply` to update
   `VERSION`. Use `minor` or `major` according to the policy above.
2. Update `docs/release-notes.md`, including any breaking changes, and review
   third-party notices against the release dependencies.
3. Build and test locally, then propose the release changes in a pull request
   from a branch into `main`.
4. Review and merge the pull request after its checks pass. Wait for passing
   hosted CI on the exact merged `main` commit, including macOS, Ubuntu,
   formatting, and the Linux sanitizer job.
5. Create and push an annotated `v<version>` tag. The release workflow enforces
   the version and CI checks before building and testing all three platforms.
6. Verify the published notes, assets, checksums, and a real installer download.

Never move a published tag or replace published assets; fixes require a new
version. For asset names, runtime requirements, and retry behavior, see the
release guide. Packaging and versioning checks use Python's standard
library and run through CTest when Python 3 is available, or directly with:

```sh
python3 tests/test_release_packaging.py
python3 tests/test_versioning.py
```

The normal CI workflow does not publish releases; the separate Release
workflow grants write permission only to its publish job.
# Progress

Use the shared `photoc_progress` API for long directory operations. Progress
renders on stderr only; commands must not print their own spinners. Update it
from the caller thread at file boundaries. Worker threads may update atomic
counters, but only the caller thread renders. Clear the active line before
ordinary stderr diagnostics, and finish progress before writing result output.

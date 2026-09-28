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

## Continuous integration

[GitHub Actions CI](.github/workflows/ci.yml) runs on pushes, pull requests,
and manual dispatches. Release builds configure CMake, compile with warnings
enabled, and run CTest on Ubuntu 24.04 and macOS 15. A separate Ubuntu Clang
Debug build enables AddressSanitizer and UndefinedBehaviorSanitizer, including
leak detection on Linux.

CI installs libexif, TurboJPEG, and native test tools so completion and man-page
checks run alongside the C and CLI tests. It does not cache dependencies or
publish releases. The sanitizer build can be reproduced locally with
`sh scripts/build-and-test-sanitizers.sh`; leak detection depends on platform
support.

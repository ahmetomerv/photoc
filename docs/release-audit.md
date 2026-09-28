# First public release audit

Audited **0.1.0** on **2026-09-28**, starting from commit `b0e34ea`, with the
release fixes in this working tree. No commands or options were added.

This is a historical audit snapshot. `focus` has since been implemented;
see [its current documentation](focus.md) for supported options and limitations.
The current source also implements [output verbosity](../README.md#output-verbosity);
the no-op verbosity limitation below describes the audited snapshot.

## Release status

The identified defects below are fixed and covered by local checks. **Require
green hosted CI for the reviewed commit before tagging.** The previously
failing CI run has not been rerun with these uncommitted changes. Native Linux
and Intel macOS builds, Linux leak detection, and actual release publication
cannot be certified by this local ARM macOS run.

## Fixed blockers

| Issue | Fix and evidence |
| --- | --- |
| Buffered stdout failures could return success, including JSON/version output. | Flush and check stdout at the CLI entry point. `output_failure` redirects help, version, metadata, and stats output to a read-only descriptor and checks failure plus file preservation. |
| Rename rollback could move another process's replacement file or symlink. | Verify the recorded source device/inode and regular-file type at the rollback destination. `rename_rollback_safety` covers normal recovery, replacement files, symlinks, and an occupied original path. |
| JPEG cleanup could unlink an unrelated pathname after `mkstemp` failed. | Track successful temporary-file creation and relinquish cleanup ownership after a successful rename. `jpeg_temp_failure_safety` injects creation failure in copy, in-place, and encoded-output paths, and verifies unrelated files and originals remain unchanged. |
| Ubuntu's sanitizer build failed to compile `test_metadata.c`. | Add POSIX feature declarations before headers for `mkdtemp` and `symlink`, retaining macOS declarations. |
| Ubuntu ShellCheck 0.9 rejected installer/uninstaller constructs. | Use explicit conditionals for receipt guards. Document the supported inode-comparison test and narrowly suppress the older POSIX warning without removing the safety check. ShellCheck 0.11 passes locally; the Ubuntu version still needs its hosted rerun. |
| macOS 15 man-page lint rejected empty URL blocks. | Give both `.UR` blocks visible labels. The existing man-page lint test and local rendered manual pass. |
| Binary distributions omitted dependency notices. | Include unaltered upstream license texts, IJG acknowledgment, and BSD notices. CMake installs license documentation; archives include notices and security/reporting guidance. The release body preserves existing notes and appends MIT/LGPL texts and dependency notices for raw downloads. Installation and archive tests compare the supplied notices to their source files. |

The hosted failures were confirmed in
[CI run 36399509368](https://github.com/ahmetomerv/photoc/actions/runs/36399509368):
Ubuntu shell lint, Ubuntu sanitizer compilation, and macOS man-page lint.

## Verification

Local environment: macOS 27.0 arm64, Apple Clang 21.0.0, CMake 4.4.3,
libexif 0.6.26, and libjpeg-turbo 3.2.0. Release binaries retain the documented
macOS 15 / Ubuntu 22.04 baselines; this newer local system does not prove them.

| Area | Result |
| --- | --- |
| Release configure, clean build, complete CTest suite | **275/275 passed**, warnings enabled and treated as errors; no compiler warnings. |
| AddressSanitizer + UndefinedBehaviorSanitizer | **275/275 passed**, no reported findings; halt-on-error enabled. |
| ThreadSanitizer, separate build | **275/275 passed**, no race reports; compile/link instrumentation verified. |
| Clang static analyzer | **51 C files** checked without findings. `clang-tidy` was unavailable. |
| C formatting | clang-format **18.1.8** check passed. |
| Shell scripts and workflows | Shell syntax tests, ShellCheck **0.11**, and actionlint **1.7.12** passed. Installer/uninstaller tests simulate all three release platforms and failed downloads/checksums, collisions, receipts, and concurrent replacement. |
| Packaging/version/install | CTest checks native executable version, archives, no-overwrite gates, checksums, canonical version/tag matching, and staged CMake installation. Dependencies are dynamically linked, not bundled. |
| Documentation/help/README | All local Markdown link targets exist. Help snapshots, command routing, option/error cases, and man-page lint pass. **51 README/help invocations** succeeded on disposable fixtures with the documented exit codes and original-preservation checks. |
| Documented JSON schemas | Parsed schemas for exif/stats/duplicates match five live reports, including missing EXIF, GPS, distributions, and duplicate groups. Existing JSON tests cover escaping and numeric/null/boolean types. |
| Malformed input and file safety | Existing tests cover invalid JPEG/EXIF, collisions, permissions, unsafe links, rollback, content preservation, and GPS removal. An additional **96 bounded malformed-input invocations** under ASan/UBSan had no crashes, findings, or original changes. This is a smoke check, not exhaustive fuzzing. |
| Licensing/provenance | Project MIT license and synthetic fixture provenance reviewed. libexif LGPL terms and libjpeg-turbo IJG/BSD terms checked against upstream notices; license copies and attribution are in `THIRD_PARTY_NOTICES.md` and `licenses/`. |

Sanitizers instrument photoc and its tests; installed dependency libraries are
not sanitizer builds. macOS does not provide the Linux job's leak-detection
coverage. Fish was absent locally, so its optional completion test was not
registered; hosted jobs install it. Sanitizer passes do not prove all inputs
or executions safe.

## Remaining known limitations

- **Scope at audit time:** image operations were JPEG-only and `focus` was a
  stub returning 3. Verbose/quiet flags do not change output. JSON exists only
  for exif, stats, and duplicates. Stable SemVer tags only; no prerelease packaging.
- **Concurrent changes and recovery:** run modifying commands only on stable,
  user-controlled directories. Identity checks are not locks. In-place scrub
  still has a check-to-rename window that can replace an intervening change;
  path-based operations can race parent-directory replacement. Rename/sort
  rollback is best effort, not a crash-safe transaction. Directory batches
  can retain earlier successful changes, and final parent-directory entries
  are not explicitly synced for power-loss durability. In-place scrub has no
  backup; ACLs and extended attributes are not preserved.
- **Metadata/privacy/color:** scrub removes EXIF GPS, not location traces in
  XMP, MakerNotes, filenames, or pixels. libexif may change proprietary tags.
  Compression is lossy, can increase size, drops ICC/XMP and other original
  marker segments, and can consequently change color rendering. An unmet
  target writes the minimum-quality copy and returns 1.
- **Malformed images/resources:** metadata inspection validates JPEG structure,
  not every entropy-coded pixel or EXIF field. Compression fully decodes into
  memory and has no hard resource quota; unusually large images can exhaust
  memory. The scaled sharpness API bounds pixel analysis but is not a CLI.
- **Reports:** timestamps have no timezone interpretation or filesystem-date
  fallback. Stats can return 0 with per-file warnings; inspect scan errors.
  Duplicate savings are logical bytes, including hard-linked paths, not
  guaranteed reclaimed storage. Files must remain stable during scans.
- **Text and names:** JSON replaces invalid UTF-8 with U+FFFD, so arbitrary
  non-UTF-8 filenames are not byte-preserving. Human output does not escape all
  terminal control characters from paths/metadata. Plan collisions use ASCII
  case folding, not full Unicode normalization; filesystem operations remain
  the final no-overwrite guard.
- **Distribution:** downloads need separately installed runtime libraries.
  macOS binaries are not developer-signed or notarized. Linux release binaries
  require x86_64/glibc 2.35+; no Linux arm64 or musl assets. No real GitHub
  release/installer download was published or exercised by this audit.

## Reproduce the release checks

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --clean-first --parallel 2
ctest --test-dir build --output-on-failure

cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DPHOTOC_SANITIZERS=ON
cmake --build build-san --parallel 2
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build-san --output-on-failure

cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DPHOTOC_THREAD_SANITIZER=ON
cmake --build build-tsan --parallel 2
TSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-tsan --output-on-failure

sh scripts/static-analysis.sh
sh scripts/format.sh --check
shellcheck --shell=sh scripts/install.sh scripts/uninstall.sh
actionlint
```

Use separate sanitizer trees. Linux CI additionally enables
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`. See
[CONTRIBUTING.md](../CONTRIBUTING.md#releases) and [releasing.md](releasing.md)
for the remaining hosted checks and tag/release process.

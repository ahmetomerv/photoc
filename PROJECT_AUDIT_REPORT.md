# Project dependency, security, and performance audit

**Audit date:** 2026-10-27

**Reviewed revision:** `dc4a09506031782ccc0bd94fb2ae03fc8244db9a` (`origin/main`)

**Scope:** read-only general scan of declared dependencies, build and release automation, security-relevant code paths, existing assurance controls, and documented performance data.

## How to use this report

- `[x]` means the audit evidence was collected in this pass.
- `[ ]` means proposed work that requires approval before implementation.
- Priority is relative to this local CLI's current threat model: **P1** should be planned first, **P2** is important hardening or reliability work, and **P3** is measurement-led improvement.
- Classifications distinguish a **confirmed vulnerability**, **known limitation**, **hardening opportunity**, and **needs validation**. A hardening item is not evidence that the project is presently exploitable.

## Executive summary

- [x] No confirmed project-code vulnerability was found in this bounded static pass.
- [x] The direct third-party libraries are minimal. The currently selected `libexif` and libjpeg-turbo releases are current upstream. The Homebrew `libxml2` formula is current, but the default local `pkg-config` lookup selected Apple's older compatibility metadata instead of Homebrew's installation; its exact patch provenance needs validation.
- [x] The project already has strong foundations: C17, warnings as errors, ASan/UBSan and TSan build options, explicit static-analysis tooling, limited GitHub Actions permissions, non-persistent checkout credentials, bounded TIFF/metadata parsing, and measurement-first benchmark documentation.
- [x] The highest-value dependency work is automation and provenance, not adding or broadly upgrading libraries: pin GitHub Actions to immutable commits, add automated update proposals, record resolved native package versions, and produce release SBOM/provenance evidence.
- [x] The highest-value security work is assurance and resource governance: persistent fuzz targets, scheduled TSan/static-analysis jobs, and explicit compressed-file/decoded-pixel limits for full JPEG decode paths.
- [x] Performance changes should not start with implementation. Existing benchmarks identify serial image commands as candidates, but representative baselines, peak-RSS budgets, deterministic-output checks, and profiles should come first.
- [x] No source, build, workflow, or configuration files were changed during this audit. This report is the only intended worktree addition.

## Scope and limitations

This is a bounded static/general scan, not a formal penetration test or exhaustive security audit. It reviewed repository code and configuration, local dependency resolution, upstream release information, existing tests/audit notes, and existing benchmark results.

The target program was not executed during this pass. Builds, unit tests, sanitizers, TSan, static analysis, fuzzers, and benchmarks were not rerun because the request was read-only, no code changed, and the available environment was not treated as an isolated hostile-input sandbox. Existing historical results are labeled as such rather than presented as current reruns. Installed system libraries are not covered by project sanitizer instrumentation.

## Dependency and tool inventory

The project has no npm, pip, Cargo, Go, Conan, or vcpkg manifest/lockfile. Native dependencies are resolved dynamically with `pkg-config` in `CMakeLists.txt:34-39`.

| Component | Declared/selected in this workspace | Current upstream or package state checked on 2026-10-27 | Assessment | Proposed action |
| --- | --- | --- | --- | --- |
| C language | C17 | Current project requirement | Appropriate | Keep C17. |
| CMake | Minimum 3.21; local 4.4.4 | 4.4.4 stable; 4.5.0-rc1 is prerelease | Current local tool; minimum preserves compatibility | Do not raise the minimum without a feature or support-policy reason. Test minimum/current instead. |
| `libexif` | Local `pkg-config`: 0.6.26 | 0.6.26 | Current upstream; release includes fixes for CVE-2026-32775, CVE-2026-40385, and CVE-2026-40386 | Keep current locally; validate distro security backports in CI/release images rather than require the upstream version string. |
| `libturbojpeg` / `libjpeg` | Local: libjpeg-turbo 3.2.0 | 3.2.0 | Current upstream | No update needed. Continue compatibility testing. |
| `libxml-2.0` | Default local `pkg-config`: Apple shim 2.9.13; Homebrew formula installed at 2.15.4 | Upstream/Homebrew 2.15.4 | Selection and patch-provenance ambiguity, not a confirmed vulnerable dependency | Make the selected path/version visible and document the intended Homebrew `PKG_CONFIG_PATH` for local builds. Test old supported and current ABI lines. |
| POSIX threads | Platform `Threads::Threads` | Platform supplied | No package update surface | No action beyond platform testing. |
| GitHub Actions | `checkout@v7`, `upload-artifact@v7`, `download-artifact@v8` | Current majors; `checkout` latest patch is v7.0.1 | Functionally current, but floating major tags are mutable | Pin full commit SHAs with version comments and automate update PRs. |
| Apple Clang | Local 21.0.0 | Platform toolchain | Suitable | Keep warnings-as-errors; test another compiler in CI where practical. |
| Python benchmark tooling | Local 3.15.1 | Helper scripts only; no Python package manifest | No third-party Python dependency inventory | Keep scripts stdlib-only where practical. |

### Distro security-version nuance

Raw upstream version floors are not reliable security gates for Linux distributions because fixes are backported. For example, Ubuntu publishes the relevant 2026 `libexif` fixes as `0.6.24-1ubuntu0.22.04.1` on 22.04 and `0.6.24-1ubuntu0.24.04.1` on 24.04. Ubuntu's fixed `libxml2` package revisions likewise retain older upstream bases. Release checks should record the full distro package revision and verify it against the distro security tracker, not reject it merely because it is numerically below the latest upstream release.

## Existing controls worth preserving

- [x] CMake enforces C17 and enables a broad warning set with `-Werror` for supported GCC/Clang-family compilers (`CMakeLists.txt:41-66`).
- [x] ASan/UBSan, TSan, and clang-tidy integrations already exist (`CMakeLists.txt:8-31`, `CMakeLists.txt:69-77`).
- [x] CI runs ASan/UBSan and small benchmark smoke checks; release and CI macOS jobs explicitly construct `PKG_CONFIG_PATH` for Homebrew libraries.
- [x] GitHub Actions permissions are scoped, and checkout steps use `persist-credentials: false`.
- [x] Release automation validates tag/version relationships and requires the exact main-branch SHA to have passed CI before publishing.
- [x] Dependencies are dynamically linked rather than bundled into release archives.
- [x] XMP parsing uses `XML_PARSE_NONET` and rejects internal/external DTD subsets (`src/core/jpeg_write.c:956-963`, `src/core/jpeg_write.c:1051-1057`).
- [x] Existing JPEG/TIFF/metadata code contains extensive size, overflow, marker, and offset validation, plus file-safety controls such as no-follow opens and no-overwrite replacement paths.
- [x] Benchmark documentation explicitly distinguishes smoke runs from tracked timings and reports peak RSS and measurement noise.

## Findings and recommended actions

### DEP-01 — Automate and harden GitHub Actions updates

**Priority:** P1

**Classification:** supply-chain hardening

**Evidence:** workflows use mutable major tags such as `actions/checkout@v7`, `actions/upload-artifact@v7`, and `actions/download-artifact@v8`; no Dependabot or Renovate configuration was found.

Mutable tags make builds less reproducible and place trust in tag movement. This is not evidence of compromise.

- [x] Pin every third-party action to a reviewed full commit SHA and retain a human-readable version comment.
- [x] Add Dependabot configuration for the `github-actions` ecosystem with a controlled cadence.
- [x] Require normal CI/review before merging automated action updates.
- [x] Document an emergency process for action security advisories.

**Acceptance criteria:** workflow references are immutable; update PRs are generated automatically; permissions and `persist-credentials: false` remain unchanged; actionlint and CI pass.

### DEP-02 — Record resolved native dependency provenance

**Priority:** P1

**Classification:** release reproducibility and supply-chain hardening

**Evidence:** apt/Homebrew dependencies are installed dynamically and can move between release runs; release artifacts do not currently include an SBOM, attestation, or a captured package-version manifest.

- [ ] Emit the resolved compiler, CMake, `pkg-config`, dependency versions, library paths, and distro package revisions in CI/release logs.
- [ ] Save a machine-readable dependency manifest with each release artifact.
- [ ] Generate an SPDX or CycloneDX SBOM for release artifacts.
- [ ] Add build provenance/attestation using GitHub's supported mechanism after deciding the required trust model.
- [ ] Verify distro backport status against vendor advisories instead of enforcing naïve upstream version floors.

**Acceptance criteria:** a release can be traced to exact source SHA, workflow/action SHAs, toolchain, resolved native packages, archive checksums, and an SBOM without rerunning the build.

### DEP-03 — Remove ambiguity from local `libxml2` selection

**Priority:** P2

**Classification:** needs validation

**Evidence:** local default `pkg-config libxml-2.0` selected Apple's 2.9.13 compatibility metadata even though Homebrew 2.15.4 is installed. CI explicitly prepends Homebrew package paths (`.github/workflows/ci.yml:69`, `.github/workflows/release.yml:98`), while the general local configure path does not make the selection obvious.

- [ ] Document the supported local `PKG_CONFIG_PATH` setup for Homebrew, reusing the CI expression.
- [ ] Print a concise configure-time summary of each dependency's selected version and resolved path.
- [ ] Add matrix coverage for the oldest supported distro package revisions and the current `libxml2` ABI line.
- [ ] Confirm whether the Apple shim maps to an Apple-patched system library before assigning it a security status.

**Acceptance criteria:** developers and release reviewers can see exactly which `libxml2` is used; both supported older packages and the current Homebrew line pass build/tests.

### SEC-01 — Add persistent fuzz coverage for untrusted parsers

**Priority:** P1

**Classification:** security assurance gap

**Evidence:** the project parses attacker-controlled JPEG markers, EXIF/TIFF structures, XMP XML, filename templates, queries, and review state. `docs/release-audit.md:56` records 96 malformed-input smoke invocations but explicitly says they are not exhaustive fuzzing. No persistent fuzz targets were found.

- [ ] Start with small, side-effect-free harnesses for TIFF/EXIF bounds parsing, JPEG marker/XMP parsing, and state/query parsing.
- [ ] Seed corpora from existing synthetic/invalid fixtures and add every minimized crash as a regression test.
- [ ] Run bounded libFuzzer or AFL++ jobs under ASan/UBSan on a schedule.
- [ ] Evaluate OSS-Fuzz only after local harnesses are deterministic, fast, and stable.
- [ ] Keep file-modifying command entry points out of initial fuzz targets; fuzz reusable core parsers instead.

**Acceptance criteria:** scheduled fuzz jobs have documented time/memory limits, preserve minimized reproducers, and produce zero unresolved crashes before releases.

### SEC-02 — Promote TSan and static analysis into hosted automation

**Priority:** P2

**Classification:** assurance hardening

**Evidence:** TSan and `scripts/static-analysis.sh` are available and historical audit results are clean, but current hosted CI does not continuously run them.

- [ ] Add a scheduled TSan build/test job on a supported runner.
- [ ] Add scheduled Clang analyzer and clang-tidy jobs with pinned/recorded tool versions.
- [ ] Keep these separate from ASan/UBSan because the sanitizer modes are incompatible.
- [ ] Define ownership and triage expectations for scheduled failures.

**Acceptance criteria:** each job is reproducible locally, uploads useful diagnostics, and has a documented policy for false positives and toolchain drift.

### SEC-03 — Define resource limits for full JPEG decoding

**Priority:** P1

**Classification:** reliability/security needs validation

**Evidence:** scaled APIs enforce dimension and file-size bounds, but the general decode entry points call `decode_jpeg_using(..., 0, 0, ...)` (`src/core/image.c:355-365`). `decode_jpeg_bytes` overflow-checks the allocation but has no policy cap for unscaled images (`src/core/image.c:239-260`). Compression and verification can fully decode input. The prior release audit already records that unusually large images can exhaust memory (`docs/release-audit.md:83-86`).

This is not classified as a confirmed vulnerability. For a local CLI, likely impact is process or machine memory pressure when a user processes an unusually large or malicious JPEG; the exact practical ceiling and command coverage need measurement.

- [ ] Inventory every full-decode call site and record which commands accept untrusted paths.
- [ ] Define explicit compressed-file, decoded-pixel, and decoded-byte budgets with user-facing failure behavior.
- [ ] Add boundary tests just below/above each limit and ensure overflow cases remain rejected.
- [ ] Measure representative high-resolution photos before choosing defaults to avoid rejecting legitimate libraries.
- [ ] Validate under ASan/UBSan and track peak RSS for both accepted and rejected inputs.

**Acceptance criteria:** all untrusted full-decode paths have documented budgets or an explicit exemption; limit failures are deterministic, do not partially modify files, and retain existing output/exit-code contracts.

### SEC-04 — Make concurrent filesystem mutation limits explicit

**Priority:** P2

**Classification:** known limitation / hardening

**Evidence:** `docs/release-audit.md:70-77` documents remaining check-to-rename and parent-directory replacement races, best-effort rollback, and incomplete crash/power-loss durability.

- [ ] Convert the documented limitations into targeted threat-model cases for scrub, rename, sort, and compression replacement.
- [ ] Decide whether supported use is restricted to stable, user-controlled directories; state that clearly in command documentation if so.
- [ ] Where stronger guarantees are required, evaluate directory-FD-relative operations, identity revalidation immediately before commit, and parent-directory sync.
- [ ] Add regression tests for any guarantee that is strengthened.

**Acceptance criteria:** the documented safety guarantee matches tested behavior; no claim implies locking or transactionality that the implementation does not provide.

### PERF-01 — Establish repeatable representative baselines

**Priority:** P1 before performance implementation

**Classification:** measurement gap

**Evidence:** CI runs only tiny smoke corpora (`.github/workflows/ci.yml:87-90`), and `benchmarks/README.md:40-42` states those timings are not tracked. Existing local baselines are useful but synthetic and host-specific.

Existing order-of-magnitude local baselines include:

| Workload | Documented throughput | Important context |
| --- | ---: | --- |
| `stats --recursive` | about 22k files/s | Synthetic small JPEG fixtures, warm cache. |
| `duplicates --recursive` | about 11–13k files/s | Strongly affected by cohort shape and prefix filtering. |
| `focus --recursive` | about 270 files/s | Improved from 147 files/s with two workers; peak RSS increased. |
| `check --recursive` | about 101 files/s | Still serial. |
| `compress --quality 80` | about 36 files/s | Still serial, full decode/encode. |
| `compress --target` | about 29 files/s | Still serial, quality search. |
| `contact --recursive` | about 127 files/s | Still serial. |

- [ ] Define small, medium, and stress corpora that resemble intended photo libraries without including private images in CI.
- [ ] Capture warm/cold-cache wall time, CPU time, peak RSS, output checksum, and error counts.
- [ ] Pin/record host, toolchain, library versions, build type, and worker count.
- [ ] Establish a noise band through repeated trials before setting regression thresholds.
- [ ] Keep smoke tests for harness health; run tracked benchmarks on a stable scheduled runner or documented reference machine.

**Acceptance criteria:** a contributor can reproduce the baseline, compare before/after JSON results, and determine whether a change exceeds noise without relying on a single run.

### PERF-02 — Profile before selecting optimization work

**Priority:** P2

**Classification:** needs measurement

**Evidence:** documentation lists plausible bottlenecks, but no current profile from representative data was produced in this pass.

- [ ] Profile `check`, quality compression, target-size compression, `contact`, and deep recursive scans separately.
- [ ] Attribute time to directory walking, metadata parsing, decode, analysis, encode, quality search, hashing, and output.
- [ ] Capture allocation/peak-RSS behavior alongside CPU profiles.
- [ ] Select only bottlenecks that reproduce on representative corpora.

**Acceptance criteria:** every optimization proposal cites a profile and includes a performance hypothesis, benchmark, correctness test, and rollback criterion.

### PERF-03 — Experiment with bounded parallelism one command at a time

**Priority:** P3 after PERF-01 and PERF-02

**Classification:** optimization candidate

**Evidence:** two-worker focus and core scanning/hashing measurements show useful scaling on some workloads. `check`, `compress`, and `contact` remain serial. Parallel full-image work also multiplies decode/analysis buffers, and `check` intentionally stays serial to bound coefficient memory.

- [ ] Prototype `check` parallelism first only if profiling shows independent per-file work dominates; define a coefficient-memory budget.
- [ ] Prototype `contact` parallel decode/thumbnail generation with deterministic layout/output ordering.
- [ ] Prototype compression parallelism last because it combines high memory, writes, progress, and failure/rollback semantics.
- [ ] For each experiment, compare 1/2/4 workers, peak RSS, CPU saturation, storage behavior, output checksums, warning order, and failure behavior.
- [ ] Keep the caller thread solely responsible for progress/output; worker threads must not print.
- [ ] Reject or cap parallelism when throughput gain is within noise or memory growth exceeds the approved budget.

**Acceptance criteria:** a change demonstrates a material, repeatable gain on an approved corpus without changing output, JSON, exit codes, file safety, progress ownership, or deterministic ordering.

### PERF-04 — Investigate I/O shape only when profiles justify it

**Priority:** P3

**Classification:** conditional optimization candidates

- [ ] Evaluate header/marker-only reads for commands that do not require full file contents.
- [ ] Reproduce deep-directory traversal costs before changing recursive-walk data structures.
- [ ] Revisit full hashing only for true duplicate/shared-prefix workloads; existing prefix filtering already removes most typical unique-file work.
- [ ] Avoid new caching or runtime dependencies until invalidation, privacy, and measurable benefit are clear.

**Acceptance criteria:** each accepted I/O change reduces measured bytes/syscalls or elapsed time and preserves unstable-file, symlink, and error semantics.

## Approval-oriented implementation phases

### Phase 0 — Evidence and inventory (completed in this report)

- [x] Inventory direct dependencies and local selected versions.
- [x] Check current upstream stable releases and relevant vendor security backports.
- [x] Review CI/release action versions, permissions, and credential persistence.
- [x] Review existing sanitizer/static-analysis/fuzz coverage.
- [x] Review documented file-safety and resource limitations.
- [x] Review existing benchmark harnesses, baselines, and measurement caveats.
- [x] Separate confirmed findings from hardening and needs-validation items.

### Phase 1 — Dependency and CI hygiene

- [x] Approve DEP-01: immutable GitHub Action pins and update automation.
- [ ] Approve DEP-03: dependency path/version visibility and compatibility matrix.
- [x] Re-run workflow lint, configure/build/tests, and sanitizer suite after changes.

**Phase gate:** no runtime behavior change; CI remains least-privileged and reproducible.

### Phase 2 — Release provenance and security assurance

- [ ] Approve DEP-02: dependency manifest, SBOM, and provenance plan.
- [ ] Approve SEC-01: persistent parser fuzz harnesses.
- [ ] Approve SEC-02: scheduled TSan/static-analysis automation.
- [ ] Approve SEC-03 discovery/tests before selecting resource-limit values.
- [ ] Decide the supported filesystem-concurrency guarantee for SEC-04.

**Phase gate:** new checks have bounded resource use, actionable diagnostics, and documented ownership; no security claim exceeds tested evidence.

### Phase 3 — Performance baseline and profiling

- [ ] Approve representative synthetic corpus definitions.
- [ ] Approve stable reference environment and noise/regression policy.
- [ ] Run PERF-01 baselines and PERF-02 profiles without implementation changes.
- [ ] Review results and choose at most one first optimization experiment.

**Phase gate:** a reproducible bottleneck and success budget exist before code changes.

### Phase 4 — Measured optimization experiments

- [ ] Implement one approved PERF-03 or PERF-04 experiment in a focused change.
- [ ] Add regression/correctness tests before or with the behavior change.
- [ ] Compare before/after wall time, CPU, peak RSS, output checksum, error behavior, and file safety.
- [ ] Keep the change only if it clears the approved gain and memory budgets.

**Phase gate:** complete standard build/CTest, ASan/UBSan, relevant TSan/static analysis, and representative benchmark comparison with no output-contract regression.

### Phase 5 — Ongoing maintenance

- [ ] Review dependency/action update PRs at least monthly.
- [ ] Review distro security advisories for the supported Ubuntu/macOS dependency set before releases.
- [ ] Run scheduled fuzzing, TSan, static analysis, and tracked performance jobs.
- [ ] Refresh this audit quarterly or before a significant release.
- [ ] Record accepted risks and deferred items with an owner and review date.

## Work that is not recommended without new evidence

- [ ] Do **not** raise minimum dependency versions merely to match upstream numbers; this could unnecessarily reject security-patched distro packages.
- [ ] Do **not** raise the CMake 3.21 minimum unless a needed feature or revised platform policy justifies it.
- [ ] Do **not** add a runtime package manager or new dependency only for update visibility; keep the native dependency set minimal.
- [ ] Do **not** parallelize image commands without correctness, peak-RSS, deterministic-output, and file-safety evidence.
- [ ] Do **not** convert the existing CI benchmark smoke timings into regression gates; the runner/corpus are not stable enough.
- [ ] Do **not** describe historical sanitizer/static-analysis results as exhaustive proof or as coverage of system libraries.

## Suggested verification commands for approved future work

Run these only after an approved implementation phase:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure

sh scripts/build-and-test-sanitizers.sh
sh scripts/static-analysis.sh

cmake -S . -B build-tsan -DPHOTOC_THREAD_SANITIZER=ON
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure
```

Performance runs should use a Release build, repeated trials, captured peak RSS, and the approved corpora. The existing entry points are documented in `benchmarks/README.md`.

## Evidence sources

### Repository evidence

- `CMakeLists.txt`
- `.github/workflows/ci.yml`
- `.github/workflows/release.yml`
- `.github/workflows/notify-homebrew-tap.yml`
- `src/core/image.c`
- `src/core/jpeg_write.c`
- `src/core/tiff.c`
- `src/core/fs.c`
- `scripts/static-analysis.sh`
- `scripts/build-and-test-sanitizers.sh`
- `benchmarks/README.md`
- `docs/release-audit.md`
- `CONTRIBUTING.md`

### Official upstream/vendor references

- [libexif releases](https://github.com/libexif/libexif/releases)
- [libjpeg-turbo releases](https://github.com/libjpeg-turbo/libjpeg-turbo/releases)
- [GNOME libxml2 source releases](https://download.gnome.org/sources/libxml2/)
- [CMake 4.4.4 release announcement](https://discourse.cmake.org/t/cmake-4-4-4-available-for-download/15862)
- [actions/checkout releases](https://github.com/actions/checkout/releases)
- [actions/upload-artifact releases](https://github.com/actions/upload-artifact/releases)
- [actions/download-artifact releases](https://github.com/actions/download-artifact/releases)
- [Ubuntu USN-8531-1](https://ubuntu.com/security/notices/USN-8531-1)
- [Ubuntu security announce, October 2026](https://lists.ubuntu.com/archives/ubuntu-security-announce/2026-October/011386.html)

### Read-only commands used during this pass

The scan used repository searches and inspection plus version/package queries such as:

```sh
git status --short --branch
git rev-parse HEAD origin/main
find . -maxdepth 3 -type f
grep -RIn PATTERN PATH...
cmake --version
cc --version
python3 --version
pkg-config --modversion libexif libturbojpeg libjpeg libxml-2.0
pkg-config --variable=pcfiledir libxml-2.0
brew list --versions libexif jpeg-turbo libxml2 cmake pkgconf
brew outdated
```

No build, test, analyzer, sanitizer, fuzzer, or benchmark command was executed in this pass.

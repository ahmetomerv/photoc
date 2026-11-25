# photoc documentation

[Project README](../README.md)

Each command guide covers usage, options, examples, special cases, safety
notes, and a JSON schema when the command supports JSON. Run
`photoc <command> --help` for a short summary, or `man photoc` for the
[manual page](../man/photoc.1).

## Review (interactive)

| Guide | Purpose |
| --- | --- |
| [`review`](review.md) | Cull JPEG and ARW photos interactively and save picked/rejected marks without changing photos |

## Inspect (read-only)

| Guide | Purpose |
| --- | --- |
| [`exif`](exif.md) | Show dimensions, camera, exposure, capture time, and GPS for one JPEG or ARW |
| [`stats`](stats.md) | Summarize storage, capture dates, cameras, lenses, and exposure settings for JPEG and ARW |
| [`timeline`](timeline.md) | Group a JPEG/ARW shoot by date and session |
| [`query`](query.md) | Find JPEG and ARW photos matching metadata filters |
| [`duplicates`](duplicates.md) | Find byte-identical files and potential space savings |
| [`focus`](focus.md) | Rank JPEG and ARW photos by sharpness score to help culling |
| [`check`](check.md) | Audit JPEG and ARW files for structural, truncation, and decoding problems |

## Organize (preview first, then `--apply`)

| Guide | Purpose |
| --- | --- |
| [`rename`](rename.md) | Rename JPEG and ARW photos from metadata templates |
| [`sort`](sort.md) | Move JPEG and ARW photos into date or session folders |

## Make copies (originals untouched by default)

| Guide | Purpose |
| --- | --- |
| [`compress`](compress.md) | Write smaller JPEG copies by quality or target size (JPEG only) |
| [`contact`](contact.md) | Generate paged JPEG contact sheets from JPEG and ARW photos |
| [`scrub`](scrub.md) | Write copies without GPS, private fields, or descriptive metadata (ARW: `--gps`/`--privacy` copies only) |

## Reference

- [Scripting and automation](scripting.md): JSON output, exit codes, progress, and verbosity.
- [Sony ARW support](raw.md): per-command behavior, metadata fields, embedded previews, limits, and fixtures.
- [Installation](installation.md): release installer, building from source, upgrading, and uninstalling.
- [Shell completions](../completions/README.md): zsh, bash, and fish.
- [Release notes](release-notes.md)

## Maintainers

- [Release guide](releasing.md): bump a version, publish it, and verify downloads.
- [Homebrew automation](homebrew-automation.md): configure and review release-triggered tap PRs.
- [Homebrew release checklist](homebrew-releases.md): update and test the tap after each release.
- [First-release audit](release-audit.md): verification results and remaining limits.

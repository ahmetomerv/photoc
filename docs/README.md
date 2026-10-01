# photoc documentation

[Project README](../README.md)

Each command guide covers usage, options, examples, special cases, safety
notes, and a JSON schema when the command supports JSON. Run
`photoc <command> --help` for a short summary, or `man photoc` for the
[manual page](../man/photoc.1).

## Inspect (read-only)

| Guide | Purpose |
| --- | --- |
| [`exif`](exif.md) | Show dimensions, camera, exposure, capture time, and GPS for one photo |
| [`stats`](stats.md) | Summarize storage, capture dates, cameras, lenses, and exposure settings |
| [`timeline`](timeline.md) | Group a shoot by date and session |
| [`query`](query.md) | Find photos matching metadata filters |
| [`duplicates`](duplicates.md) | Find byte-identical files and potential space savings |
| [`focus`](focus.md) | Rank JPEGs by sharpness score to help culling |
| [`check`](check.md) | Audit JPEGs for structural and decoding problems |

## Organize (preview first, then `--apply`)

| Guide | Purpose |
| --- | --- |
| [`rename`](rename.md) | Rename photos from metadata templates |
| [`sort`](sort.md) | Move photos into date or session folders |

## Make copies (originals untouched by default)

| Guide | Purpose |
| --- | --- |
| [`compress`](compress.md) | Write smaller JPEG copies by quality or target size |
| [`contact`](contact.md) | Generate paged JPEG contact sheets |
| [`scrub`](scrub.md) | Write copies without GPS, private fields, or descriptive metadata |

## Reference

- [Scripting and automation](scripting.md): JSON output, exit codes, progress, and verbosity.
- [Sony ARW metadata support](raw.md): supported fields, limits, and fixtures.
- [Installation](installation.md): release installer, building from source, upgrading, and uninstalling.
- [Shell completions](../completions/README.md): zsh, bash, and fish.
- [Release notes](release-notes.md)

## Maintainers

- [Release guide](releasing.md): bump a version, publish it, and verify downloads.
- [First-release audit](release-audit.md): verification results and remaining limits.

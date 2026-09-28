# photoc rename

[Command overview](../README.md#commands)

## Purpose

Build filenames from JPEG metadata and preview or apply renames. Each photo
stays in its current directory; use [sort](sort.md) to organize folders.
**Dry run is the default.** Files change only with `--apply`.

## Syntax

```text
photoc rename <directory> --format <template> [--recursive] [--apply]
photoc rename --help
```

Exactly one directory and one template are required. Quote the template and
paths containing spaces. Option values must be separate arguments, not
`--format=...`.

## Options

| Option | Meaning |
| --- | --- |
| `--format <template>` | Required filename template; placeholders are case-sensitive. |
| `--recursive` | Include JPEGs in nested directories; the default scans only the supplied directory. |
| `--apply` | Rename files only after the entire plan passes preflight. |
| `-h`, `--help` | Print command help and exit successfully. |
| `-v`, `--verbose` | Recognized; currently does not change output. |
| `-q`, `--quiet` | Recognized; currently does not change output. Cannot be combined with verbose. |

There is no `--dry-run` flag: omit `--apply`. `--json` is unsupported and returns
exit status 2. Sequence width is currently fixed by the command, with no CLI
option to change it.

## Templates

| Placeholder | Expansion | Example |
| --- | --- | --- |
| `{date}` | Valid EXIF capture date as `YYYY-MM-DD` | `2026-09-27` |
| `{datetime}` | Valid EXIF capture time as `YYYY-MM-DD_HH-MM-SS` | `2026-09-27_12-34-56` |
| `{camera}` | Camera model | `Model Z` |
| `{make}` | Camera make | `Camera Co` |
| `{iso}` | ISO, without a prefix | `200` |
| `{aperture}` | Aperture number, without `f/` | `2.8` |
| `{focal}` | Focal length in millimeters, without a unit | `50` |
| `{sequence}` | One-based sequence, minimum four digits | `0001` |
| `{original}` | Original basename without its final extension | `IMG_001` |
| `{ext}` | Original extension without a dot, preserving case | `JPG` |

Include the dot yourself: `{original}_{sequence}.{ext}`. The command does not
automatically append an extension. Use `{{` and `}}` for literal braces.
Unknown placeholders, unbalanced braces, an empty template, and results of
`.` or `..` are invalid.

ASCII control characters, DEL, and `/`, `\`, `:`, `*`, `?`, `"`, `<`, `>`, `|`
become underscores in both literal text and substituted values. Spaces are
preserved. A slash in a template creates an underscore, not a subdirectory.
Different inputs can sanitize to the same name, so collision checks still
apply.

JPEG paths are sorted lexically before numbering, across the entire scan.
Numbers start at `0001`, are not truncated after `9999`, and include entries
that later prove blocked. Skipped non-JPEGs do not consume a number. Sequence
assignments are deterministic for the same set of source paths, not stable
across additions, removals, or previous renames.

## Examples

```sh
# Preview an EXIF-based naming scheme.
photoc rename ./photos --format "{date}_{camera}_{sequence}.{ext}"

# Works for valid JPEGs without EXIF: uses only filename and sequence.
photoc rename ./photos --format "{original}_{sequence}.{ext}" --recursive

# Apply after reviewing the preview. This rebuilds and rechecks the plan.
photoc rename ./photos --format "{date}_{camera}_{sequence}.{ext}" --apply

# End option parsing before a directory name beginning with a dash.
photoc rename --format "{original}_{sequence}.{ext}" -- -photos
```

An illustrative preview row is:

```text
IMG_001.JPG -> 2026-09-27_Model Z_0001.JPG
```

Safe plan rows and the summary go to stdout. Blocked entries and reasons go
to stderr. The summary counts JPEG candidates, planned changes, unchanged
paths, blocked entries, applied renames, and rolled-back renames. In dry run,
applied and rolled-back counts are zero; unchanged rows are marked.

## Edge cases

- Only regular `.jpg`/`.jpeg` files are selected, case-insensitively. Symlinks
  and non-JPEG files are ignored; symlinked directories are not followed.
- Missing metadata blocks a photo only when a requested placeholder needs it.
  Invalid capture dates block `{date}`/`{datetime}`. There is no filesystem-date
  fallback or timezone conversion. A broken/unreadable JPEG blocks its rename
  even if the template uses only its filename.
- Duplicate proposed destination paths are blocked using ASCII
  case-insensitive comparison, conservatively covering common macOS
  filesystems. This is not full Unicode case folding.
- An existing destination is blocked, even if another source would move away
  from that path. Swaps and rename cycles are not resolved.
- Source and destination exactly equal is an unchanged entry. A case-only
  rename can be blocked on a case-insensitive filesystem.
- Identical new basenames in different directories are allowed when their full
  destination paths differ. There is no collection-wide basename uniqueness
  requirement.
- An empty scan succeeds with zero counts. A dry run with any blocked entry
  returns **1**, while still printing safe rows and reasons.
- Filesystem filename-length limits or changes made by another process can
  still cause an apply failure after planning.

## Safety notes

Preflight loads metadata, expands templates, checks destinations and duplicate
names, and rechecks sources before apply. **Any blocked entry prevents all
renames in that invocation.** Existing files are never overwritten; there is
no force option. Renaming does not re-encode images or edit EXIF.

If a rename fails after earlier changes, the command attempts to restore those
paths in reverse order. This is not a crash-safe transaction: rollback can
fail if another process changes the filesystem. Review stderr and applied/
rolled-back counts after a failure, and keep backups for important work.
Avoid concurrent edits while applying.

## Exit statuses

| Status | Meaning |
| --- | --- |
| `0` | Preview or apply completed with no blocked entries; unchanged/empty plans are allowed. |
| `1` | Blocked plan, scanning/planning failure, apply/rollback failure, or output failure. |
| `2` | Missing arguments, invalid template syntax/placeholders, or unsupported/conflicting CLI options. |

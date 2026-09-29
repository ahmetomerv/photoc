# photoc sort

[Command overview](../README.md#commands) · [Output verbosity](../README.md#output-verbosity)

## Purpose

Organize JPEG/ARW photos into folders under a directory, by EXIF capture date or by
capture-time session. Original filenames are preserved. **Dry run is the
default**; `--apply` creates destination directories and moves files.

Sony `.arw` files are supported for common TIFF/EXIF metadata, with the same
command safety/exit behavior. Other RAW formats are unsupported. This is
metadata-only support: see [ARW fields and limits](raw.md).

## Syntax

```text
photoc sort <directory> --by date [--recursive] [--apply]
photoc sort <directory> --by session [--gap <duration>] [--recursive] [--apply]
photoc sort --help
```

Exactly one directory and one sort mode are required. Give option values as
separate arguments, such as `--by date`, not `--by=date`.

## Options

| Option | Meaning |
| --- | --- |
| `--by date` | Propose `YYYY/MM/DD/<original filename>` under the input root. |
| `--by session` | Propose `session-001/<original filename>`, `session-002/`, and so on. |
| `--gap <duration>` | Maximum gap between consecutive photos in a session; default **60 minutes**. Only valid with `--by session`. |
| `--recursive` | Include nested directories; the default scans only the supplied directory. |
| `--apply` | Move files only after the entire plan passes preflight. |
| `-h`, `--help` | Print command help and exit successfully. |
| `-v`, `--verbose` | Report date/session and preview/apply modes, recursion, session gap, discovery/skip/metadata-failure counts, and applied/rolled-back counts on stderr. |
| `-q`, `--quiet` | Keep plan mappings; hide the summary. Blocked entries and apply/rollback errors remain visible. |

Global verbosity flags work before or after the command. Combining quiet and
verbose returns usage status **2**. JSON, where supported, has the same schema
in every mode; all diagnostics and errors use stderr.

Gap values are nonnegative integer minutes (`30m`) or hours (`2h`), with
lowercase suffixes. `0m` is valid: only equal timestamps stay together.
Fractional values, bare numbers, seconds, uppercase suffixes, and values that
overflow the supported minute count are rejected.

There is no `--dry-run` flag: omit `--apply`. `--json` is unsupported and returns
exit status 2. There is no separate destination-root option.

## Grouping rules

Capture time comes from valid EXIF `DateTimeOriginal` in
`YYYY:MM:DD HH:MM:SS` form. There is no filesystem-date fallback or timezone
conversion.

- **Date:** use the recorded year/month/day. A photo captured on September 27,
  2026 goes to `2026/09/27/`.
- **Session:** sort valid capture times chronologically, using source path to
  break equal-time ties. Start a new session when the gap from the previous
  photo **exceeds** the threshold. An exact-threshold gap stays in the session.
  Session length can exceed the gap because the rule compares consecutive
  photos, not each photo with the first one.

Session IDs start at 1 with a minimum width of three digits. They are stable
for the same input set and gap; adding or removing photos can merge/split
sessions and change IDs. All valid dated photos in a recursive scan participate
in the same grouping. Missing/invalid dates are blocked and excluded from
grouping.

Use [timeline](timeline.md) for a read-only chronological overview without
proposing moves. Timeline reuses the gap rule but splits sessions at midnight
to group by calendar date; sort sessions may cross calendar boundaries.

## Examples

```sh
# Preview; no directories or files are changed.
photoc sort ./photos --by date
photoc sort "Photo archive" --by date --recursive
photoc sort ./photos --by session --gap 30m
photoc sort ./photos --by session --gap 2h --recursive

# Apply after reviewing the same preview.
photoc sort ./photos --by date --recursive --apply

photoc sort --by session --gap 30m -- -photos
```

Illustrative plan rows:

```text
IMG_001.JPG -> 2026/09/27/IMG_001.JPG
```

```text
IMG_001.JPG -> session-001/IMG_001.JPG
```

Rows are printed in source-path order, with paths relative to the input root.
Safe rows and the summary go to stdout; blocked paths and reasons go to stderr.
The summary reports JPEG/ARW candidates, planned moves, unchanged paths, blocked
entries, applied moves, and rolled-back moves. In dry run, applied and
rolled-back counts are zero.

## Edge cases

- Only regular `.jpg`/`.jpeg`/`.arw` files are selected, case-insensitively. Symlinks,
  unsupported files, and symlinked directories are not followed.
- A missing/invalid capture date or broken/unreadable photo blocks that entry.
  Dry run prints the remaining safe moves and returns **1**; apply with any
  blocked entry moves no files.
- Existing destination files and duplicate proposed destinations are blocked.
  Duplicate plan paths use ASCII case-insensitive comparison; full Unicode
  case folding is not performed. Two photos with the same filename destined
  for the same date/session folder conflict.
- A destination parent that is a file or symlink blocks the move. Apply refuses
  a symlink as the scan root and uses directory descriptors to avoid traversing
  symlinked destination parents.
- A source already at its exact destination is unchanged. Flat scans do not
  revisit photos already inside date/session subdirectories; use `--recursive`
  when reviewing an existing organized tree.
- Existing session folder names do not represent persistent sessions. Rerunning
  on a different subset can propose different IDs or produce collisions.
- Empty scans succeed with zero counts. Moving across filesystem boundaries
  fails; there is no copy-and-delete fallback. Empty original directories are
  not removed after successful moves.

## Safety notes

Preflight checks the entire move plan, source identities, destination
collisions, and parent directories. **Any blocked entry prevents all moves.**
No existing file is overwritten and no image data or EXIF is re-encoded.
Dry run creates no directories.

Apply creates required destination directories before moving files. It uses
exclusive moves and rechecks sources. If a move fails, it attempts to reverse
earlier moves and remove directories created by this operation. Cleanup or
rollback can fail after concurrent filesystem changes, so this is not a
crash-safe transaction. Check stderr and the final counts; keep backups and
avoid simultaneous modifications. New directories use normal POSIX creation
permissions filtered by your umask.

Use [rename](rename.md) for metadata-based filenames and [exif](exif.md) to
inspect the capture dates used for grouping.

## Exit statuses

| Status | Meaning |
| --- | --- |
| `0` | Preview or apply completed without blocked entries; unchanged/empty plans are allowed. |
| `1` | Blocked plan, scanning/planning failure, apply/rollback/cleanup failure, or output failure. |
| `2` | Missing arguments, invalid mode/gap, incompatible options, or unsupported CLI options. |

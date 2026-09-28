# photoc scrub

[Command overview](../README.md#commands)

## Purpose

Remove the EXIF GPS directory and its pointer from JPEGs. By default, write a
new `.scrubbed` copy beside each source and keep the original unchanged.
**`--in-place` explicitly replaces originals and creates no backup.**

The command rewrites EXIF without recompressing JPEG image data. This is EXIF
GPS removal, not removal of all metadata or a guarantee of anonymity.

## Syntax

```text
photoc scrub <file|directory> --gps [--recursive] [--in-place]
photoc scrub --help
```

Exactly one regular JPEG file or directory is required. JPEG recognition uses
`.jpg` and `.jpeg`, case-insensitive.

## Options

| Option | Meaning |
| --- | --- |
| `--gps` | Required; remove all EXIF GPS IFD entries and the root GPS pointer. |
| `--recursive` | Include nested directories. Invalid with a single-file input. |
| `--in-place` | Replace each original only after writing and verifying a temporary file. No backup is created. |
| `-h`, `--help` | Print command help and exit successfully. |
| `-v`, `--verbose` | Recognized; currently does not change output. |
| `-q`, `--quiet` | Recognized; currently does not change output. Cannot be combined with verbose. |

There is no dry-run or `--apply` option: copy mode writes immediately, and
`--in-place` authorizes replacement. `--json` is unsupported and returns exit
status 2. There is no JSON schema or destination-directory option.

## Examples

```sh
# Write photo.scrubbed.jpg if EXIF GPS tags exist.
photoc scrub photo.jpg --gps
photoc scrub "Summer trip/IMG_001.JPG" --gps
photoc scrub ./photos --gps --recursive

# Inspect the result; exif is a separate read-only command.
photoc exif photo.scrubbed.jpg

# Explicit replacement: make your own backup first.
photoc scrub photo.jpg --gps --in-place
photoc scrub ./photos --gps --recursive --in-place

photoc scrub --gps -- -photo.jpg
```

Copy names preserve the original extension casing: `IMG_001.JPG` becomes
`IMG_001.scrubbed.JPG`. Directory mode writes copies in each source directory,
not into one common destination.

## Output

Processed files print `GPS removed: source -> destination`, or
`GPS removed in place: source`. JPEGs without EXIF GPS print a skip reason.
Directory candidates are processed in source-path order.

The summary reports:

- files processed: successful GPS removals;
- files skipped: files without GPS and excluded entries such as non-JPEGs;
- files failed: per-file failures, plus a traversal failure if one occurred;
- files with GPS found and removed: successful removals, not every file in
  which GPS was found before a later writing failure.

Normal output and summaries go to stdout; failures go to stderr.

## Edge cases

- A JPEG without EXIF, or with EXIF but no GPS tags, is skipped successfully.
  No copy is created and no replacement occurs. An empty directory also
  succeeds with zero totals.
- GPS detection checks for GPS directory entries or a GPS pointer, even if
  the coordinate data is incomplete. This is broader than
  [exif](exif.md)'s `has_gps`, which requires a valid coordinate pair.
- An existing `.scrubbed` destination causes a failure without overwriting it.
  Directory processing continues with other candidates and exits **1**.
- Directory mode excludes files whose stem already ends in `.scrubbed`, as
  well as non-JPEG and non-regular entries. A directly supplied JPEG with that
  suffix is still inspected. Symlinks and symlinked directories are not
  followed.
- Broken JPEGs, invalid EXIF, ambiguous/unsafe JPEG layouts, oversized rewritten
  EXIF, and unreadable/unwritable paths can fail. A directory traversal failure
  prevents processing the collected candidates; individual edit failures do
  not stop the other candidates.
- `--in-place` refuses symlinks, hard-linked files, files owned by another user,
  and sources whose identity or contents changed since metadata was loaded.
  If mode bits or group ownership cannot be preserved, replacement fails.

## Safety notes

### Copy mode

The original is never changed. A temporary JPEG beside the destination is
written, synced, and verified before an atomic no-overwrite rename. All JPEG
bytes outside the rewritten EXIF segment, including compressed image data,
are copied unchanged. Enough space and directory write permission are needed
even when the source itself is read only.

Non-GPS EXIF is retained where libexif can represent it, including ordinary
camera and exposure fields. Unusual MakerNotes may change during serialization.
New copies do not promise to retain source filesystem permissions, ownership,
ACLs, or extended attributes.

### In-place mode

A temporary JPEG is written beside the source, verified, and atomically
renamed over that source only after safety checks. Failures before replacement
leave the original path unchanged. POSIX permission bits and group ownership
are preserved; ACLs and extended attributes are **not copied**. Replacing the
file also changes its filesystem identity and can change timestamps.

There is no backup or undo file. A directory operation is not a transaction:
if a later file fails, earlier successful replacements remain. Keep your own
backups and avoid concurrent modifications.

### Privacy scope

GPS in XMP, MakerNotes, other metadata formats, filenames, or visible image
content is outside this command's removal scope. Non-EXIF JPEG segments are
preserved, so any location data there may remain. Retained capture times and
camera identifiers can also be sensitive. Review the output before sharing;
`exif` reporting `GPS: No` alone is not proof that every location trace is gone.

## Exit statuses

| Status | Meaning |
| --- | --- |
| `0` | Completed with no failures; skips and no-GPS files are allowed. |
| `1` | Unsupported input, unsafe source, invalid JPEG/EXIF, collision, traversal, writing, or output failure. Other files may already be processed. |
| `2` | Missing path/`--gps`, recursive mode with a file, or unsupported/conflicting CLI options. |

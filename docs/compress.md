# photoc compress

[Command overview](../README.md#commands) · [Output verbosity](../README.md#output-verbosity)

## Purpose

Re-encode a JPEG or a directory of JPEGs with a chosen quality, or search for
the highest quality that fits a target file size. This is **lossy compression**.
The original stays unchanged; the command writes new copies immediately.

JPEG recognition uses `.jpg` and `.jpeg`, case-insensitive. Encoding uses
libjpeg-turbo and 4:2:0 chroma subsampling. Dimensions are retained; there is
no resize option.

## Syntax

```text
photoc compress <file|directory> [--quality <1-100>] [--recursive] [--output-dir <directory>]
photoc compress <file|directory> --target <size> [--min-quality <1-100>] [--recursive] [--output-dir <directory>]
photoc compress --help
```

Exactly one input path is required. Give option values as separate arguments,
for example `--quality 75`, not `--quality=75`.

## Options

| Option | Meaning |
| --- | --- |
| `--quality <1-100>` | JPEG quality; default **80**. Cannot be combined with `--target`. |
| `--target <size>` | Maximum desired output size, including copied EXIF. Search qualities from the minimum through 100. |
| `--min-quality <1-100>` | Minimum quality for target search; default **20**. Requires `--target`. |
| `--recursive` | Include nested directories. Invalid with a single-file input. |
| `--output-dir <directory>` | Write copies under this directory, creating parents as needed. Preserve paths relative to the input directory. |
| `-h`, `--help` | Print command help and exit successfully. |
| `-v`, `--verbose` | Report mode, recursion, quality settings, input/output paths, and processing/skip/failure counts on stderr. |
| `-q`, `--quiet` | Hide compression status and summaries; file operations are unchanged. Errors and unmet-target messages remain visible. |

Global verbosity flags work before or after the command. Combining quiet and
verbose returns usage status **2**. JSON, where supported, has the same schema
in every mode; all diagnostics and errors use stderr.

Size syntax is a positive integer followed by an optional, case-sensitive
suffix: bytes with no suffix or `B`, decimal `KB`/`MB`/`GB`, or binary
`KiB`/`MiB`/`GiB`. `2MB` means 2,000,000 bytes; `2MiB` means 2,097,152 bytes.
Decimals such as `1.5MB`, spaces inside the value, lowercase suffixes, and
zero are rejected.

There is no `--apply`, dry-run, or in-place mode. `--json` is unsupported and
returns exit status 2.

## Examples

```sh
# Write photo.compressed.jpg at the default quality of 80.
photoc compress photo.jpg

photoc compress "Summer trip/IMG_001.JPG" --quality 75
photoc compress photo.jpg --target 2MB --min-quality 30

# photos/day1/IMG_001.JPG becomes compressed/day1/IMG_001.compressed.JPG.
photoc compress ./photos --recursive --quality 75 --output-dir ./compressed

# Put options before -- when a path starts with a dash.
photoc compress --quality 80 -- -photo.jpg
```

Without `--output-dir`, each output is placed beside its source. The suffix
`.compressed` is inserted before the extension, preserving extension casing.
For a single file with an output directory, only its filename is carried over.

## Output

Single-file output shows the destination, chosen quality, original and output
sizes, bytes saved, and percentage saved. Target mode also shows the target
size and whether it was met. Savings can be negative.

Directory output lists processed files and existing-output skips in source-path
order, then reports processed, skipped, and failed counts, bytes before/after,
and total savings. Target mode adds a count of targets not met. Storage totals
include only files for which an output was successfully written, including
best-effort outputs that missed their target.

Normal output goes to stdout; failures and target-miss diagnostics go to stderr.

## Edge cases

- An unreachable target still writes a best-effort copy at the minimum quality,
  reports the achieved size, and returns **1**. EXIF overhead alone can make a
  very small target impossible. Target search assumes size generally grows
  with quality; it is not a guarantee of a globally optimal result.
- Quality 100 is still a re-encode, not a lossless copy. A compressed output can
  be larger than its source; that alone is not a failure.
- An existing output fails a single-file operation. In directory mode it is
  skipped without causing failure on its own.
- Directory scans skip non-JPEG files, symlinks, and files whose stem already
  ends in `.compressed`. A directly supplied `.compressed.jpg` can be processed
  again. Empty directories finish with zero totals.
- Flat scanning is the default. Nested directories are included only with
  `--recursive`; symlinks are not followed. A separately named output directory
  is excluded from candidate collection, but avoid using alternate spellings
  of the same directory.
- Broken or unreadable JPEGs fail individually; directory processing continues.
  A directory traversal failure prevents processing the collected candidates.

## Safety notes

Outputs are written to temporary files, verified, and finalized without
overwriting an existing destination. The command needs write access and enough
space for the output and temporary data. Destination directory components that
are symlinks are refused; use a real directory path.

EXIF is copied where libexif can represent it, **including GPS**. This is not a
privacy scrub. ICC profiles, XMP, and other original JPEG marker segments are
not copied; unusual EXIF/MakerNotes may also change. Keep masters and inspect
the copies before sharing or replacing any files yourself. See
[scrub](scrub.md) for EXIF GPS removal and [exif](exif.md) for inspection.

Directory compression is not a transaction: a later failure leaves earlier
successful copies in place. Originals remain unchanged. There is no promise
to preserve filesystem ownership, permissions, ACLs, or extended attributes
on new copies.

## Exit statuses

| Status | Meaning |
| --- | --- |
| `0` | Completed; directory skips are allowed and all requested targets were met. |
| `1` | File, directory, encoding, writing, or output failure, or at least one target not met. A copy may already exist. |
| `2` | Invalid arguments, quality/size, incompatible options, or unsupported CLI option. |

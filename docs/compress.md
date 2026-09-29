# photoc compress

[Command overview](../README.md#commands) · [Output verbosity](../README.md#output-verbosity)

## Purpose

Re-encode a JPEG or a directory of JPEGs with a chosen quality, or search for
the highest quality that fits a target file size. This is **lossy compression**.
The original stays unchanged; the command writes new copies immediately.

JPEG recognition uses `.jpg` and `.jpeg`, case-insensitive. Encoding uses
libjpeg-turbo and 4:2:0 chroma subsampling for RGB inputs. Grayscale inputs with
ICC profiles stay one-component grayscale. Dimensions are retained; there is
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
| `--target <size>` | Maximum desired output size, including preserved EXIF, ICC, and XMP. Search qualities from the minimum through 100. |
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
  reports the achieved size, and returns **1**. Preserved metadata overhead
  alone can make a very small target impossible. Target search assumes size
  generally grows with quality; it is not a guarantee of a globally optimal result.
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

EXIF, ICC, and XMP are preserved by default, **including GPS and any location
information in XMP**. This is not a privacy scrub. Keep masters and inspect the
copies before sharing or replacing any files yourself. See [scrub](scrub.md)
for EXIF GPS removal and [exif](exif.md) for inspection.

Directory compression is not a transaction: a later failure leaves earlier
successful copies in place. Originals remain unchanged. There is no promise
to preserve filesystem ownership, permissions, ACLs, or extended attributes
on new copies.

## Metadata preservation

Both quality and target-size modes, including directory compression, preserve
these recognized JPEG metadata segments in their original relative order:

| Category | Exact behavior |
| --- | --- |
| EXIF APP1 (`Exif` signature) | Preserve the complete payload, including Orientation, GPS, and MakerNotes, after the existing libexif validity checks. Compression no longer rebuilds EXIF. Duplicate EXIF segments or EXIF after a scan remain rejected. |
| ICC APP2 (`ICC_PROFILE` signature) | Preserve every chunk header and profile byte exactly, including valid chunks stored out of sequence. Require consistent nonzero chunk counts, unique indices from 1 through the count, all chunks present, and nonempty total profile data. |
| Standard Adobe XMP APP1 | Preserve the entire packet with the `http://ns.adobe.com/xap/1.0/` signature, including its terminating NUL. |
| Extended Adobe XMP APP1 | Preserve all packets with the `http://ns.adobe.com/xmp/extension/` signature verbatim, including GUIDs, lengths, offsets, and chunk data in their original order. Packets are copied as opaque data, without assembling or interpreting the extended document. |

JPEG segment lengths are checked before reading their payloads. ICC chunk
validation checks the JPEG packaging only; photoc does not parse, repair,
replace, or convert ICC profile contents. XMP XML, GUID associations, and
extended packet completeness are not validated or repaired: existing packet
contents, including malformed contents, are retained exactly. No XMP fields
are updated to describe the newly compressed file.

Recognized metadata after the first scan is refused rather than relocated or
silently dropped. Metadata snapshots are limited to **64 MiB**, including
segment bookkeeping; exceeding the limit fails that file. Malformed JPEG
lengths, incomplete/inconsistent ICC chunks, or an unsupported metadata layout
return **1** without publishing a compressed copy for that file. In directory
mode, other successful copies remain available.

Other original APP markers, comments, Photoshop/IPTC APP13 resources, MPF/MPO
image-offset data, and proprietary metadata are not preserved. Coding tables,
scan data, and encoder-specific markers are generated afresh. The encoder's
own JFIF marker is retained. Original file permissions, extended attributes,
and sidecar files are outside this metadata support.

Raw pixel dimensions and EXIF Orientation are retained; compression does not
rotate pixels according to Orientation. Grayscale JPEGs with ICC remain
grayscale so the profile is not attached to an RGB conversion. The existing
RGB decoder does not support CMYK/YCCK JPEGs. No color-management conversion
is added. Tests cover RGB and grayscale ICC transport, all eight EXIF
orientation values, standard XMP, and multi-packet Extended XMP.

Before publishing a copy, photoc verifies its metadata bytes/order, checks its
raw dimensions and ICC-associated component count, and decodes it successfully.
Target-size searches include the complete preserved marker overhead. Large
metadata can therefore make a requested target impossible.

Format references: [libjpeg-turbo ICC chunk handling](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/src/jdicc.c)
and [Adobe JPEG XMP layout](https://github.com/adobe/XMP-Toolkit-SDK/blob/main/XMPFiles/source/FileHandlers/JPEG_Handler.cpp).
No upstream implementation code or additional library is bundled.

## Exit statuses

| Status | Meaning |
| --- | --- |
| `0` | Completed; directory skips are allowed and all requested targets were met. |
| `1` | File, directory, encoding, writing, or output failure, or at least one target not met. A copy may already exist. |
| `2` | Invalid arguments, quality/size, incompatible options, or unsupported CLI option. |

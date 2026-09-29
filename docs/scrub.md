# photoc scrub

[Command overview](../README.md#commands) · [Output verbosity](../README.md#output-verbosity)

## Purpose and syntax

```text
photoc scrub <file|directory> (--gps|--privacy|--all-metadata) [--recursive] [--in-place]
```

Choose **exactly one** mode. The modes are mutually exclusive; they do not
compose. `--gps` retains the original behavior: it removes only the EXIF GPS
IFD and pointer, skips JPEGs without EXIF GPS, and leaves every other JPEG
segment byte for byte. `--privacy` removes recognized location and personal
identifier fields. `--all-metadata` removes supported descriptive metadata
while keeping color and orientation information needed for display.

By default, a changed JPEG becomes a `.scrubbed` copy beside the original.
`photo.JPG` becomes `photo.scrubbed.JPG`. An unchanged JPEG is skipped and no
copy is made. `--in-place` explicitly replaces the original, with no backup.
JPEG pixel/scan data is never recompressed or rotated.

## Modes

| Mode | Removed | Retained |
| --- | --- | --- |
| `--gps` | EXIF GPS IFD entries and root pointer only | Every other JPEG byte, including XMP, IPTC, ICC, and MakerNotes |
| `--privacy` | EXIF GPS; standard EXIF Artist, CameraOwnerName, BodySerialNumber, LensSerialNumber, ImageUniqueID; recognized standard Adobe XMP GPS, location, creator/owner, serial and document-ID properties; IPTC IIM location, byline, credit, source, contact and caption-writer datasets in a standard Photoshop APP13 resource | Other EXIF fields, XMP properties and IPTC datasets; ICC; orientation; JPEG structure and scan data |
| `--all-metadata` | EXIF except a valid Orientation tag; standard and Extended Adobe XMP APP1 packets; Photoshop/IPTC APP13 segments; JPEG COM comments | ICC APP2 chunks byte for byte; JFIF/Adobe and structural JPEG markers; compressed scan data; valid Orientation value 1–8 in a minimal EXIF APP1 segment |

`--privacy` uses libexif for EXIF and libxml2 for standard XMP. Those libraries
are required at build and run time. XMP is matched by namespace and property name,
not a text search. IPTC removal edits individual datasets and keeps unrelated
Photoshop resources. `--all-metadata` does not reencode pixels: it retains the
Orientation tag, so a viewer that honored the original tag will display the
same rotation. If EXIF has no Orientation tag, a valid standard XMP
`tiff:Orientation` value is moved into a minimal EXIF tag. Otherwise the EXIF
segment is removed.
ICC bytes and chunk order are unchanged; no color conversion occurs.

Unknown APP markers, unsupported APP1/APP2 payloads, and other opaque formats
are kept. `--all-metadata` is therefore a removal of **supported descriptive
metadata**, not a claim that no metadata bytes remain. A file with an MPF APP2
index is refused if a rewrite is needed, because its embedded offsets would
otherwise become stale. Other proprietary marker offsets are not rewritten;
inspect such files separately before use.

## Examples

```sh
photoc scrub photo.jpg --gps
photoc scrub photo.jpg --privacy
photoc scrub photo.jpg --all-metadata
photoc scrub ./photos --privacy --recursive
photoc scrub photo.jpg --privacy --in-place
photoc scrub --gps -- -photo.jpg
```

JPEG recognition uses `.jpg` and `.jpeg`, case-insensitive. Directory mode
processes candidate paths in sorted order, skips non-JPEGs and existing
`.scrubbed` stems, and does not follow symlinks. A directly supplied JPEG with
that suffix is still inspected. `--recursive` requires a directory. `--json`
and `--apply` are unsupported.

Normal status and summaries go to stdout, errors and warnings to stderr.
`-v`/`--verbose` lists **categories** found and removed, never coordinates,
serial numbers or names. `-q`/`--quiet` suppresses informational output and
noncritical metadata warnings, but not failures. `--gps` retains its original
status lines and GPS count; the broader modes report processed, skipped and
failed files.

## Safety and privacy limits

Copy publication uses a temporary file in the destination directory, syncs it,
checks the edited bytes and JPEG structure, then renames without overwrite. An
existing `.scrubbed` file is never replaced. In-place mode uses a temporary file
beside the source and repeats source identity checks before atomic replacement.
It refuses symlinks, hard links, different owners and changed sources, and
preserves POSIX mode bits and group ownership. ACLs, extended attributes and
filesystem identity may change. There is no backup. A directory operation is
not transactional across files, and concurrent path changes still have a
check-to-rename window.

Selective EXIF rewriting can change opaque MakerNote bytes or offsets. Privacy
mode **does not parse or guarantee sanitization of MakerNotes**; camera serials,
location or other personal data may remain inside them. When a MakerNote or
unsupported marker is detected, the command warns without showing its values.
An unrecognized Photoshop APP13 layout is retained with a warning. A malformed
recognized IPTC layout fails rather than risking a partial edit. Privacy mode
also refuses Extended XMP and malformed standard XMP without creating/replacing
a file, because it cannot guarantee selective removal there. All-metadata mode
refuses malformed standard XMP and an Extended XMP document without a separate
known Orientation value, since that document could contain the only rotation
instruction. Unknown XMP namespaces and proprietary APP markers may retain
sensitive fields. Free-text fields kept in EXIF, XMP or IPTC can also contain
private details.

File names, sidecar files, visible scene content, thumbnails inside unrecognized
markers, and metadata formats outside the listed support are not sanitized.
Review the output before sharing. `photoc exif` reporting `GPS: No` is not
proof that every location trace is gone.

## Exit statuses

| Status | Meaning |
| --- | --- |
| `0` | No failures; unchanged or excluded files may have been skipped. |
| `1` | Input, metadata, safety, collision, traversal, or I/O failure. Other files in a directory may already have been processed. |
| `2` | Missing path/mode, multiple modes, or invalid/conflicting options. |

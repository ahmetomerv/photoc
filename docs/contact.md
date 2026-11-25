# photoc contact

[Command overview](../README.md#commands) · [Output verbosity](scripting.md#output-verbosity)

Generate JPEG contact sheets from a directory of JPEG and Sony ARW
photographs. Originals are read only. The command needs an explicit output path ending in `.jpg` or
`.jpeg` and never replaces an existing output.

```sh
photoc contact ./photos --output sheet.jpg
photoc contact ./photos --output sheet.jpg --metadata --sort date --recursive
```

| Option | Behavior |
| --- | --- |
| `--output <file.jpg>` | Required output name. One page uses this exact path; multiple pages use `-001`, `-002`, etc. before the extension. |
| `--recursive` | Include nested directories; symlinks are not followed. |
| `--columns <1-8>` | Columns per page; default 4. |
| `--thumb-size <96-512>` | Square thumbnail box size in pixels; default 240. Images fit within the box without stretching. |
| `--quality <1-100>` | JPEG output quality; default 85. |
| `--metadata` | Show available aperture/shutter and ISO/focal length below each filename. Missing fields are left blank. |
| `--sort name\|date` | Filename order by default, with path as a tie breaker. Date uses EXIF capture time; undated photos follow dated photos in filename order. |

The built-in, original 5×7 bitmap font has no external font dependency or
attribution requirement. Labels use at most the thumbnail width and shorten
long names with `~`. Non-ASCII characters render as `?`.

EXIF orientation is applied while drawing each thumbnail, including mirrored
orientations. ARW thumbnails are drawn from the JPEG preview the camera embeds
in the file, typically 1616 x 1080, with the ARW's orientation applied; RAW
sensor data is never decoded, so the thumbnail shows the camera's own
rendering. The sheet is a new JPEG containing rendered pixels only; source
EXIF, XMP, IPTC, GPS, and ICC profiles are not copied. There is no color-profile
conversion, so photos with non-sRGB ICC profiles may have different colors in
the sheet. The output is for review, not a color-managed proof.

Each page holds at most 24 photos and six rows. A scan is limited to 1,000
photos, individual source JPEGs to 64 MiB, and ARW embedded previews to
64 MiB (the ARW itself may be larger; only its preview is read). The RGB canvas is limited to
64 MiB and 8,192 pixels on each side; an over-limit layout fails safely. The
renderer holds one decoded source image at a time. The JPEG decoder uses a
bounded scale and may reject very large dimensions. Other files are skipped.

All output paths are checked before the first page is rendered. Each page is
written through a verified temporary JPEG and published without overwriting.
If a later source fails to decode or a later page fails to write, earlier
completed pages remain, and the command returns status 1. A bad JPEG, an ARW
without a usable embedded preview, or unreadable photo metadata also returns
status 1. Empty input returns status 1;
invalid options return status 2. Status text goes to stdout; diagnostics go
to stderr. `--quiet` suppresses status text, and `--verbose` adds page details
to stderr. `--json` is unsupported.

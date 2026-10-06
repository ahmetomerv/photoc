# photoc review

[Command overview](../README.md#commands) · [File safety](../README.md#file-safety)

Interactively cull a directory of JPEG photos. Move through the discovered
collection, inspect available metadata and a sharpness score, and mark each
photo **picked**, **rejected**, or **unmarked**. Marks survive quitting and
restarting. Review never edits, moves, or deletes a JPEG.

**Automatic image previews appear only in a detected direct iTerm2 session.**
In other terminals, review still works but shows filenames, metadata,
sharpness, and controls without the photo. A terminal that understands the
iTerm2 protocol may use `--images iterm` to force a preview attempt.

## Syntax and options

```text
photoc review <directory> [--recursive] [--sort name|date]
                          [--show all|unmarked|picked|rejected]
                          [--images auto|iterm|none] [--state <file>]
```

Exactly one directory is required. Review discovers regular `.jpg` and
`.jpeg` files, ignoring extension case. It scans only that directory by
default; `--recursive` includes nested directories. Scans do not follow
symlinks. A single JPEG, Sony ARW, other RAW files, HEIC, and `--json` are
not supported. Both stdin and stdout must be terminals.

| Option | Behavior |
| --- | --- |
| `--recursive` | Include nested directories. |
| `--sort name` | Default: case-sensitive, bytewise basename order, then relative path for equal names. |
| `--sort date` | Valid EXIF capture timestamps first, oldest first; identical dates and undated photos use relative path order. No timezone conversion or filesystem-date fallback. |
| `--show all\|unmarked\|picked\|rejected` | Filter navigation; `all` is the default. Counts still describe the entire discovered collection. |
| `--images auto\|iterm\|none` | `auto` is the default; see [Terminal images](#terminal-images). |
| `--state <file>` | Use a different review-state file. Its parent directory must already exist. |

Options take separate values, for example `--sort date`, and can appear
before or after `review`. `-q`/`--quiet` suppresses informational text after
leaving the UI, while errors and the active UI remain visible. `-v`/`--verbose`
does not add lines inside the UI. Review does not run a progress spinner, so
`--no-progress` does not change the interface.

## Keyboard controls

| Key | Action |
| --- | --- |
| Right Arrow, `l`, Space | Next photo. |
| Left Arrow, `h` | Previous photo. |
| `p` | Pick the current photo. |
| `x` | Reject the current photo. |
| `u` | Remove its mark. |
| `i` | Toggle compact and detailed metadata. |
| `?` | Toggle help. |
| `q` | Quit and show a short summary. |
| Ctrl+C | Interrupt cleanly, preserving marks already saved. |

Letter shortcuts accept either uppercase or lowercase input. A lone Escape
key is ignored and does not consume the next shortcut.

The screen puts Pick, Reject, and Unmark in a separate action row with colored
keycaps and text labels. The bracketed keys and labels remain visible without
color; set `NO_COLOR=1` to disable action colors.

Each mark is saved before the selection and counts change on screen. After
`p`, `x`, or `u`, review advances if there is a next matching photo. In a
filtered view, a changed photo that no longer matches disappears from
navigation; the next matching photo becomes current, or the previous one if
it was last. If none remain, the UI says so and still accepts `q`. Unknown
keys are ignored.

Metadata includes available camera, dimensions, exposure, and capture time.
Unavailable fields are omitted. `i` shows a more detailed view. Sharpness is
calculated only when a photo is first displayed; its score or failure is
cached for the session. It is a review aid, not an automatic classification
or quality verdict. Review never marks a photo automatically.

## Save and resume

The default state file is `<review-root>/.photoc-review.json`. It is created
on the first actual status change; browsing and quitting without changes
does not create one. `--state <file>` changes its location. The file stores a
version, the canonical absolute review root, and only picked or rejected
relative paths:

```json
{
  "version": 1,
  "root": "/absolute/path/to/shoot",
  "items": [
    {"path": "DSC00001.JPG", "status": "picked"},
    {"path": "nested/DSC00002.JPG", "status": "rejected"}
  ]
}
```

Missing entries are unmarked. If a filesystem path is not valid UTF-8, the
same bytes are stored as `root_bytes_hex` or `path_bytes_hex` instead of the
corresponding text field. Do not edit these fields unless you preserve their
encoding and root identity.

On restart, review restores marks and counts, then starts at the first photo
in the requested sort and filter. It does not restore cursor position. State
entries for photos absent from the current discovery are preserved when the
state is next saved. A state file for another root, an unsupported version,
malformed JSON, or an unrelated existing file is refused rather than
overwritten.

```sh
photoc review ./shoot
# Quit, then continue with photos that have no mark:
photoc review ./shoot --show unmarked
```

Each changed mark is written to a unique temporary file in the state file's
directory, checked and synced, then atomically renamed into place. Review
refuses state-file symlinks and detects a state file changed by another
process. If a save fails, the current mark and counts stay at their last
saved values, an error appears in the UI, and the final exit status is 1.
The previous valid state is retained.

## Terminal images

`--images auto` sends an iTerm2 inline JPEG only when a direct iTerm session
is detected from `TERM_PROGRAM=iTerm.app` and `ITERM_SESSION_ID`, excluding
tmux, screen, and a dumb terminal. Otherwise it behaves like `none`.
`--images iterm` forces that backend when detection misses a supported
terminal; it cannot add image support to a terminal that lacks the protocol.
`--images none` uses text only and is useful in any ANSI terminal.
All modes show the filename, position, status, metadata, sharpness, counts,
and controls.

The iTerm backend streams the original JPEG bytes without generating a copy
or decoding it for display. iTerm2 decides how to show EXIF orientation;
photoc does not rotate pixels. The single-sequence V1 backend is not designed
for tmux passthrough. If an image or metadata becomes unreadable or disappears
during review, the UI shows it as unavailable and navigation continues. The
discovered list is fixed for the session; review does not rescan automatically.

The interface restores terminal input settings and leaves its alternate
screen on normal quit, handled interruption, and controlled I/O failures.
Untrusted filename and metadata bytes, including control characters, are
escaped before terminal display.

## File safety and limits

The **only** file review creates or updates is its review-state JSON. It never
changes JPEG bytes, EXIF, filenames, or locations; it never deletes or moves
picked or rejected files. Keep the state file if you want to resume later.
Review is not a database or a photo editor, and V1 has no RAW previews,
ratings, thumbnail cache, mouse controls, or additional image protocols.

Exit status is **0** on success, **2** for invalid usage, and **1** for
operational failure (including no TTY, a refused state file, or a failed
save). An empty directory or empty startup filter exits successfully without
entering raw mode.

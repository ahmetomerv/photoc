# Scripting and automation

[Project README](../README.md) · [Command guides](README.md)

photoc is designed to be used in shell scripts. Requested results go to
stdout; status text, warnings, errors, and progress go to stderr. This guide
covers JSON output, exit codes, progress, and output verbosity.

## JSON output

Add `--json` to `query`, `check`, `exif`, `stats`, `timeline`, `duplicates`, or
`focus` to get output for scripts and other tools. You can also save it to a
file:

```sh
photoc exif photo.jpg --json
photoc stats ./photos --recursive --json > stats.json
photoc duplicates ./photos --recursive --json > duplicates.json
photoc focus ./photos --recursive --json > focus.json

# Optional: use jq to select exposure metadata.
photoc exif photo.jpg --json | jq '.exposure'
```

Example exposure object:

```json
{
  "iso": 200,
  "aperture": 2.8,
  "exposure_time_seconds": 0.008,
  "focal_length_mm": 50
}
```

Missing values are `null`. Fields such as `has_gps` use `true` or `false`, and
numbers stay numeric. In JSON mode, stdout contains only JSON; diagnostics,
warnings, and errors use stderr. Quiet mode suppresses non-critical warnings.
`jq` is optional and is not required to run photoc.

Each [command guide](README.md) documents its JSON schema.

## Paths for other tools

`query` prints matching paths, one per line, sorted. Use `--print0` for
NUL-separated paths that are safe with any filename:

```sh
photoc query ./photos --recursive --has-gps --print0 |
  xargs -0 sh -c 'for path do photoc exif -- "$path" || exit; done' sh
```

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | Success |
| `1` | A processing or filesystem operation failed, or a compression target was not met |
| `2` | Invalid command usage |
| `3` | The command is not implemented (reserved; no current command returns it) |

`stats` and `timeline` can return success even when individual files produce
warnings. Their reports count those errors (`stats` JSON `scan.errors`,
`timeline` JSON `summary.metadata_errors`). When completeness matters, use
normal output or inspect `--json`, including in quiet mode. `query` returns
**1** when a JPEG load fails, while still listing successful directory
matches; no matches returns **0**. `check` returns **0** for warnings and
**1** for errors or incomplete checks.

## Progress output

Longer-running directory commands display progress when stderr is connected
to a terminal. Progress shows a spinner with a running processed count, plus
a percentage when the command knows the total up front. Progress is written
only to stderr, so stdout remains safe for scripts and JSON:

```sh
photoc stats ./photos --recursive --json > stats.json
```

Use `--no-progress` to disable it. Progress is also disabled when stderr is
redirected or `--quiet` is used. Fast operations finish before the spinner's
200 ms display delay and produce no progress line.

## Output verbosity

Global flags work before or after the command:

| Mode | Behavior |
| --- | --- |
| Default | Show results, operation summaries, and warnings. |
| `-q`, `--quiet` | Keep requested results; hide status text, summaries, and non-critical warnings. |
| `-v`, `--verbose` | Keep normal output and add diagnostics on stderr. |
| `--no-progress` | Disable the interactive progress display. |

**Quiet mode** keeps every requested result: EXIF fields, statistics and
distributions, duplicate groups and savings, focus scores, query paths, check
rows and counts, timeline sessions with skipped and error counts, and
rename/sort mappings. It hides scan status and rename/sort summaries.
Successful `compress` and `scrub` runs print nothing to stdout; their file
operations and exit codes stay the same. Errors that explain a non-zero exit
remain on stderr, including partial duplicate/focus results and blocked
rename/sort plans.

**Verbose mode** adds the operating mode, recursion, discovery/skip/failure
counts, and relevant paths. Statistics, timeline, and duplicate scans also
report the worker limit; small workloads or worker startup failures can run
serially.

With `--json`, stdout remains JSON only, with the same fields in every mode.
Verbose diagnostics use stderr, and quiet JSON still includes scan/error
counters. Help and version output remain available in every mode. Combining
quiet and verbose is a usage error (exit **2**), regardless of option order.

```sh
# Keep the requested duplicate report, suppress scan status.
photoc duplicates ./photos --quiet

# Save JSON and diagnostics separately.
photoc --verbose stats ./photos --recursive --json > stats.json 2> diagnostics.log

# Remove GPS in new copies, showing only failures.
photoc scrub ./photos --gps --quiet
```

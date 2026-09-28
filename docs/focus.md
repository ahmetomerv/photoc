# photoc focus

[Command overview](../README.md#commands)

## Purpose

Compare JPEG sharpness using variance of a Laplacian on grayscale pixels.
Reports list the supplied file path, or paths relative to a scanned directory,
and a numeric sharpness score. Rows are sorted by **ascending score**, so
photos with less edge detail appear first. Exact score ties use case-sensitive,
bytewise path ordering.

Scores below a chosen threshold are labeled **possibly blurry**. This is a
review hint, not proof of blur or a measure of artistic quality.

## Syntax

```text
photoc focus <file|directory> [--recursive] [--threshold <value>] [--only-blurry]
photoc focus --help
```

Exactly one regular JPEG or directory is required. JPEG recognition uses
`.jpg` and `.jpeg`, ignoring case. EXIF metadata is not required.

## Options

| Option | Meaning |
| --- | --- |
| `--recursive` | Include nested directories. Invalid with a single file. |
| `--threshold <value>` | Finite, nonnegative review cutoff; default **100**. Decimal and scientific notation are accepted. |
| `--only-blurry` | List only scores strictly below the threshold. Uses the default threshold if omitted. |
| `-h`, `--help` | Print command help and exit successfully. |
| `-v`, `--verbose` | Recognized; currently does not change output. |
| `-q`, `--quiet` | Recognized; currently does not change output. Cannot be combined with verbose. |

Option values are separate arguments, for example `--threshold 50.5`.
`--json` is not supported yet and returns exit status 2; no JSON schema is
defined for this command.

## Examples

```sh
photoc focus photo.jpg
photoc focus "Photo archive" --recursive
photoc focus ./photos --threshold 50.5 --only-blurry
photoc focus ./photos --recursive --threshold 100 --only-blurry
photoc focus --threshold 0 -- -photo.jpg
```

The default threshold is a starting point for review, not a universal blur
boundary. Calibrate it on comparable photos that you have inspected yourself.

## Output and summary

Each row contains a filename/path, score rounded to three decimal places,
and an optional `possibly blurry` label. Filtering uses the unrounded score;
a score equal to the threshold is not labeled. A threshold of zero labels
no photos, since scores are nonnegative.

The summary includes:

- Photos successfully analyzed and the number below the threshold.
- Regular non-JPEG files skipped and JPEG files that failed analysis.
- Minimum, average, and maximum score across all successfully analyzed photos.

`--only-blurry` filters the displayed rows, **not the summary**. A report with
no matching rows can still describe successfully analyzed photos. No scores
are available for an empty directory or a scan where every JPEG failed;
minimum, average, and maximum then display `Unavailable`.

## Edge cases and metric limitations

- Missing EXIF is valid. Broken, unreadable, or undecodable JPEGs produce
  diagnostics on stderr. Directory analysis continues past individual failures
  and returns status 1 if any JPEG failed. Fatal traversal/allocation errors
  fail without a normal report.
- Non-JPEG regular files in directories are skipped. A directly supplied
  non-JPEG, symlink, or other non-regular file is rejected. Scans never follow
  symlinks; directories and non-regular entries do not enter summary counts.
- Analysis uses the shared [sharpness API](../include/photoc/sharpness.h) and
  scaled decoding with a maximum dimension of 1024. It decodes one image at a
  time, retains paths and scores for sorting, and reads compressed JPEG bytes
  into memory. Extremely large images may not fit a supported decoder scale.
- Higher values indicate more local edge variation. Noise, texture, sharpening,
  JPEG artifacts, and subject matter can raise the score. A flat, well-focused
  subject can score low.
- The metric cannot identify the intended subject, distinguish motion blur
  from defocus, or judge composition. Scores depend on image resolution,
  analysis scale, and decoder build. Compare similar inputs; equal settings
  do not make the score an absolute sharpness measurement.

## Safety notes

The command is read only. It does not move, rename, delete, or rewrite photos.
Normal reports go to stdout and diagnostics go to stderr. Review flagged
photos yourself before making decisions about them. Files should remain
unchanged during analysis for consistent results.

## Exit statuses

| Status | Meaning |
| --- | --- |
| `0` | Analysis completed without failures, or help printed. Low scores alone do not cause failure. |
| `1` | Input, decoding, traversal, allocation, or output failure. A partial report may include successfully analyzed photos. |
| `2` | Invalid arguments, threshold, incompatible options, or unsupported JSON mode. |

# photoc focus

[Command overview](../README.md#commands)

## Purpose and current status

`focus` is reserved for JPEG sharpness analysis. **The command is not
implemented.** It does not decode photos, produce scores, rank files, or
provide JSON output. The help text describes planned syntax only.

A shared C sharpness API already exists, but it is not exposed through this
command. See the [sharpness API header](../include/photoc/sharpness.h) for its
current contract.

## Syntax

```text
photoc focus --help
photoc focus <photo>...
```

`--help` works today. The second line is planned syntax and currently routes
to the unimplemented-command message. The placeholder does not validate photo
count or inspect the supplied paths.

## Options

| Option | Current behavior |
| --- | --- |
| `-h`, `--help` | Print planned command usage and exit successfully. |
| `-v`, `--verbose` | Recognized; does not enable analysis or change output. |
| `-q`, `--quiet` | Recognized; does not suppress the unimplemented diagnostic. Cannot be combined with verbose. |
| `--json` | Recognized globally, but no focus JSON exists. Invocation still returns the unimplemented status. |

There are no focus-specific options. `--recursive` and other unknown options
are usage errors. No JSON schema is defined for a command that cannot yet
produce a report.

## Examples

```sh
# Available now.
photoc focus --help

# Planned operation: currently exits 3 without reading the file.
photoc focus photo.jpg
```

The diagnostic goes to stderr:

```text
photoc: focus is not implemented yet
```

Ordinary invocation writes nothing to stdout. A missing or unsupported path
still reaches the placeholder; it is not evidence that the file is readable
or supported. Invalid CLI flags/conflicts can fail earlier with status 2.

## Edge cases and metric limitations

The existing internal metric uses population variance of a four-neighbor
Laplacian on grayscale pixels, with scaled JPEG decoding and a recommended
maximum analysis dimension of 1024. Its score is deterministic for the same
input, analysis size, and decoder build. Those are API details, **not available
CLI settings**.

Higher values indicate more local edge variation. Noise, texture, sharpening,
and JPEG artifacts can raise the score; a flat, well-focused subject can score
low. The metric does not identify the intended subject, distinguish motion
blur from defocus, or assess composition or artistic quality. There is no
universal sharp/blurred threshold. Comparisons require the same analysis size.

## Safety notes

The placeholder reads no photo data and changes no files. There is no apply,
deletion, or selection workflow. Do not use its exit status as a sharpness
decision in scripts.

## Exit statuses

| Status | Meaning |
| --- | --- |
| `0` | Help printed. |
| `2` | Invalid or conflicting CLI options. |
| `3` | Command is not implemented, including a normal invocation with `--json`. |

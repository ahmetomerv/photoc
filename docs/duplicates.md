# photoc duplicates

[Command overview](../README.md#commands) · [Output verbosity](scripting.md#output-verbosity)

## Purpose

Report exact duplicate files in a directory. This command compares regular
files of **any type**, not only JPEGs. It uses file size, a leading-content
filter, and SHA-256; it does not compare visual similarity or decoded pixels.
Nothing is deleted or modified.

## Syntax

```text
photoc duplicates <directory> [--recursive] [--json]
photoc duplicates --help
```

Exactly one directory is required. Relative and absolute paths are accepted.

## Options

| Option | Meaning |
| --- | --- |
| `--recursive` | Include nested directories; the default scans only the supplied directory. |
| `--json` | Emit a JSON summary and duplicate groups. |
| `-h`, `--help` | Print command help and exit successfully. |
| `-v`, `--verbose` | Report mode, recursion, scanned/hashed/skipped files, failures, and worker limit on stderr. |
| `-q`, `--quiet` | Keep duplicate groups, counts, and savings; hide scan status. Warnings explaining a failed scan remain visible. |

Global verbosity flags work before or after the command. Combining quiet and
verbose returns usage status **2**. JSON, where supported, has the same schema
in every mode; all diagnostics and errors use stderr.

There is no deletion, apply, or dry-run option: the command is always read only.

## Examples

```sh
photoc duplicates ./photos
photoc duplicates "Photo archive" --recursive
photoc duplicates ./photos --recursive --json > duplicates.json
photoc duplicates --recursive -- -archive

# Optional jq: list the paths in each duplicate group.
photoc duplicates ./photos --json | jq '.groups[].paths'
```

## Output and counting

The terminal report lists each group, its file size, and all member paths,
then shows group count, duplicate-file count, potential storage savings, and
files scanned/hashed/skipped. No groups produces `No duplicates found.`
Displayed sizes use decimal B, KB, MB, GB, TB, PB, or EB; JSON retains
exact numeric byte values.

- **Duplicate files** counts every member of groups with at least two paths,
  including the copy that could be retained. A group of three counts as three.
- **Potential savings** assumes one retained copy per group:
  `(number of files - 1) * file size`, summed across groups.
- Full-file hashes are only needed for candidates that share a size and
  content prefix. Empty files use the known empty SHA-256 digest. The hashed
  count is not the same as the visited count.
- Groups are ordered by ascending file size, then digest; paths within each
  group are ordered lexically. This ordering is deterministic for unchanged
  inputs, including when internal hashing uses bounded concurrency.

## Edge cases

- Same-size files with different bytes are not duplicates. Files with the same
  pixels but different metadata or encoding are also not exact duplicates.
- Multiple empty files form a duplicate group with zero potential savings.
  An empty directory or a directory without duplicates succeeds.
- Symlinks and other non-regular entries are ignored. Symlinked directories
  are not followed; a symlink cannot be used as the scan root.
- Unreadable files are warned about on stderr and skipped; readable groups
  are still reported, but the command returns **1**. A fatal directory walk
  error prevents the normal report.
- Files should remain unchanged during scanning. The report is not a snapshot
  or a reservation of the reported paths.
- Hard-linked paths are not deduplicated by inode. They can appear in a group
  even though they already share storage. Potential savings describe logical
  file bytes, not guaranteed freed space on hard-linked, sparse, compressed,
  or copy-on-write filesystems.

## Safety notes

The command reads files without changing them or choosing which copy to keep.
Review group membership and your backup needs before taking any later action.
Reports contain local paths; avoid sharing them unintentionally.

Normal reports and JSON go to stdout; warnings/errors go to stderr. JSON can
be complete while the exit status is non-zero after file warnings. Check both
the status and stderr before treating the result as a complete inventory.

## JSON schema

The JSON output includes the fields below, but does **not** include the terminal
report's visited/hashed/skipped counters or an error count. SHA-256 is a
64-character lowercase hexadecimal string. `paths` contains all group members,
using paths based on the supplied directory spelling.

Schema for the current report:

```json
{
  "title": "photoc duplicates output",
  "type": "object",
  "required": ["directory", "recursive", "duplicate_group_count", "duplicate_file_count", "potential_savings_bytes", "groups"],
  "additionalProperties": false,
  "properties": {
    "directory": {"type": "string"},
    "recursive": {"type": "boolean"},
    "duplicate_group_count": {"type": "integer", "minimum": 0},
    "duplicate_file_count": {"type": "integer", "minimum": 0},
    "potential_savings_bytes": {"type": "integer", "minimum": 0},
    "groups": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["file_size_bytes", "sha256", "paths"],
        "additionalProperties": false,
        "properties": {
          "file_size_bytes": {"type": "integer", "minimum": 0},
          "sha256": {"type": "string", "pattern": "^[0-9a-f]{64}$"},
          "paths": {"type": "array", "minItems": 2, "items": {"type": "string"}}
        }
      }
    }
  }
}
```

For no duplicates, `groups` is `[]` and all three aggregate counts/savings
are zero. There is no JSON error envelope. An output failure can leave a
partial document.

## Exit statuses

| Status | Meaning |
| --- | --- |
| `0` | Scan and report completed without file errors, even if no duplicates exist. |
| `1` | At least one file could not be read, or a fatal scan/output failure occurred. |
| `2` | Invalid argument count or unsupported/conflicting CLI options. |

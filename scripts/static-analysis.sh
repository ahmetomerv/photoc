#!/bin/sh
# Run clang-tidy when it is installed, then the Clang static analyzer
# (clang --analyze) using the CMake compilation database.
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
build="$root/build-analyze"

cmake -S "$root" -B "$build" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

if command -v clang-tidy >/dev/null 2>&1; then
    sources=$(find "$root/src" -name '*.c' | sort)
    # shellcheck disable=SC2086
    clang-tidy -p "$build" --warnings-as-errors='*' $sources
else
    echo "static-analysis: clang-tidy was not found; analyzer results still run." >&2
    echo "static-analysis: macOS: brew install llvm && export PATH=\"\$(brew --prefix llvm)/bin:\$PATH\"" >&2
    echo "static-analysis: Debian/Ubuntu: sudo apt install clang-tidy" >&2
    echo "static-analysis: Fedora: sudo dnf install clang-tools-extra" >&2
fi

if ! command -v clang >/dev/null 2>&1; then
    echo "static-analysis: clang was not found, so the static analyzer did not run." >&2
    exit 1
fi

python3 - "$build/compile_commands.json" "$root" <<'PY'
import json
import shlex
import subprocess
import sys

database_path, root = sys.argv[1], sys.argv[2]
entries = json.load(open(database_path))
seen = set()
failed = False
for entry in entries:
    source = entry["file"]
    if not source.startswith(root + "/") or not source.endswith(".c"):
        continue
    if "/src/" not in source and "/tests/" not in source:
        continue
    if source in seen:
        continue
    seen.add(source)
    parts = shlex.split(entry["command"])
    command = ["clang", "--analyze",
               "-Xclang", "-analyzer-output=text",
               "-Xclang", "-analyzer-disable-checker",
               "-Xclang",
               "security.insecureAPI.DeprecatedOrUnsafeBufferHandling"]
    skip_next = False
    for part in parts[1:]:
        if skip_next:
            skip_next = False
            continue
        if part == "-o":
            skip_next = True
            continue
        if part == "-c":
            continue
        command.append(part)
    result = subprocess.run(command, cwd=entry["directory"],
                            capture_output=True, text=True)
    diagnostics = []
    for line in (result.stderr + result.stdout).splitlines():
        if "warning:" in line or "error:" in line:
            diagnostics.append(line)
    if diagnostics or result.returncode != 0:
        failed = True
        print(f"{source}:", file=sys.stderr)
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
if failed:
    sys.exit(1)
print(f"static-analysis: clang --analyze checked {len(seen)} files")
PY

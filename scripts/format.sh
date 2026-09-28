#!/bin/sh
set -eu

if [ "$#" -gt 1 ]; then
    echo "Usage: sh scripts/format.sh [--check|--write]" >&2
    exit 2
fi
case "${1:---check}" in
    --check) set -- --dry-run --Werror ;;
    --write) set -- -i ;;
    *)
        echo "Usage: sh scripts/format.sh [--check|--write]" >&2
        exit 2
        ;;
esac

cd "$(dirname "$0")/.."
if [ -n "${CLANG_FORMAT:-}" ]; then
    formatter=$CLANG_FORMAT
elif command -v clang-format-18 >/dev/null 2>&1; then
    formatter=clang-format-18
else
    formatter=clang-format
fi
if ! command -v "$formatter" >/dev/null 2>&1; then
    echo "clang-format 18 is required; see CONTRIBUTING.md." >&2
    exit 1
fi
case "$("$formatter" --version)" in
    *"version 18."*) ;;
    *)
        echo "clang-format 18 is required; set CLANG_FORMAT to its executable." >&2
        exit 1
        ;;
esac

# Only project C sources/headers; do not follow symlinks or enter excluded trees.
find src include tests benchmarks \
    \( -type d \( -name fixtures -o -name generated -o -name vendor \
        -o -name third_party \) -prune \) -o \
    \( -type f \( -name '*.c' -o -name '*.h' \) \
        -exec "$formatter" --style=file "$@" -- {} + \)

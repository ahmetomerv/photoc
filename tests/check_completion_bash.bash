#!/usr/bin/env bash
set -eu
source "$1"
test_root=$(mktemp -d "$2/completion-bash.XXXXXX")
trap 'rm -rf "$test_root"' EXIT
cd "$test_root"
mkdir 'Photo album'
touch 'Photo image.JPG' ordinary.txt

query()
{
    COMP_WORDS=("$@")
    COMP_CWORD=$((${#COMP_WORDS[@]} - 1))
    _photoc_complete
}

has()
{
    local match
    for match in "${COMPREPLY[@]}"; do
        [[ $match != "$1" ]] || return 0
    done
    printf 'Missing completion: %s (got: %s)\n' "$1" "${COMPREPLY[*]}" >&2
    exit 1
}

lacks()
{
    local match
    for match in "${COMPREPLY[@]}"; do
        if [[ $match == "$1" ]]; then
            printf 'Unexpected completion: %s\n' "$1" >&2
            exit 1
        fi
    done
}

query photoc ''
for name in compress exif duplicates stats rename sort focus scrub; do has "$name"; done
query photoc --ver
has --version
query photoc -v st
has stats
query photoc --json du
has duplicates
for name in exif duplicates stats; do
    query photoc "$name" --
    has --json
    lacks --version
done
for name in compress rename sort focus scrub; do
    query photoc "$name" --
    lacks --json
done
for name in compress duplicates stats rename sort focus scrub; do
    query photoc "$name" --rec
    has --recursive
done
query photoc rename --
has --format; has --apply; lacks --gps
query photoc sort --
has --by; has --gap; has --apply
query photoc scrub --
has --gps; has --in-place; lacks --apply
query photoc focus --
has --threshold; has --only-blurry; lacks --apply; lacks --json
query photoc --threshold 100 focus --only
has --only-blurry
query photoc focus ./ --threshold ''
[[ ${#COMPREPLY[@]} == 0 ]]
query photoc focus 'Photo'
has 'Photo album'; has 'Photo image.JPG'
query photoc focus ./ ''
[[ ${#COMPREPLY[@]} == 0 ]]
query photoc compress --
for flag in --quality --target --min-quality --output-dir; do has "$flag"; done
query photoc sort ./ --by ''
has date; has session; lacks compress
query photoc compress ./ --quality 8
has 80; has 85
query photoc sort ./ --gap 3
has 30m
query photoc compress ./ --target 2
has 2MB
query photoc compress ./ --min-quality 3
has 30
query photoc rename ./ --format ''
[[ ${#COMPREPLY[@]} == 0 ]]
query photoc compress --output-dir stats --
has --quality; lacks --json
query photoc --quality 80 compress --out
has --output-dir
query photoc --quality 80 co
has compress
query photoc sort stats --
has --by; lacks --json
query photoc stats 'Photo'
has 'Photo album'; lacks 'Photo image.JPG'
query photoc exif 'Photo'
has 'Photo image.JPG'
query photoc compress ./ --output-dir 'Photo'
has 'Photo album'; lacks 'Photo image.JPG'
query photoc exif -- --
lacks --help; lacks --json
query photoc stats ./ ''
[[ ${#COMPREPLY[@]} == 0 ]]
complete -p photoc >/dev/null
printf 'Bash completion checks passed.\n'

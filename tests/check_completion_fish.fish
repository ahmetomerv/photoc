#!/usr/bin/env fish
source $argv[1]; or exit 1
set -l test_root (mktemp -d "$argv[2]/completion-fish.XXXXXX"); or exit 1
function photoc_test_cleanup --on-event fish_exit --inherit-variable test_root
    rm -rf -- "$test_root"
end
cd "$test_root"; or exit 1
mkdir 'Photo album'
touch 'Photo image.JPG' ordinary.txt

function query
    set -g suggestions (complete -C "$argv[1]" | string split -f 1 \t)
end
function has
    contains -- "$argv[1]" $suggestions; and return 0
    printf 'Missing completion: %s (got: %s)\n' "$argv[1]" "$suggestions" >&2
    exit 1
end
function lacks
    if contains -- "$argv[1]" $suggestions
        printf 'Unexpected completion: %s\n' "$argv[1]" >&2
        exit 1
    end
end

query 'photoc '
for name in check compress exif duplicates stats rename sort focus scrub
    has $name
end
query 'photoc --ver'
has --version
query 'photoc -v st'
has stats
for name in check exif duplicates stats focus
    query "photoc $name --"
    has --json
    lacks --version
end
for name in compress rename sort scrub
    query "photoc $name --"
    lacks --json
end
for name in check compress duplicates stats rename sort focus scrub
    query "photoc $name --rec"
    has --recursive
end
query 'photoc rename --'
has --format; has --apply; lacks --gps
query 'photoc scrub --'
has --gps; has --in-place; lacks --apply
query 'photoc focus --'
has --threshold; has --only-blurry; has --json; lacks --apply
query 'photoc --threshold 100 focus --only'
has --only-blurry
query 'photoc focus Photo'
has 'Photo album/'; has 'Photo image.JPG'
query 'photoc compress --'
for flag in --quality --target --min-quality --output-dir
    has $flag
end
query 'photoc sort ./ --by '
has date; has session
query 'photoc compress ./ --quality 8'
has 80; has 85
query 'photoc sort ./ --gap 3'
has 30m
query 'photoc compress --output-dir stats --'
has --quality; lacks --json
query 'photoc --quality 80 compress --out'
has --output-dir
query 'photoc sort stats --'
has --by; lacks --json
query 'photoc stats Photo'
has 'Photo album/'; lacks 'Photo image.JPG'
query 'photoc exif Photo'
has 'Photo image.JPG'
query 'photoc compress ./ --output-dir Photo'
has 'Photo album/'; lacks 'Photo image.JPG'
query 'photoc exif -- --'
lacks --help; lacks --json

query 'photoc check --'
has --only-errors; has --recursive; has --json; lacks --apply; lacks --threshold
query 'photoc check Photo'
has 'Photo album/'; has 'Photo image.JPG'
query 'photoc exif --'
lacks --only-errors
printf 'fish completion checks passed.\n'

#!/usr/bin/env zsh
setopt errexit nounset
zmodload zsh/zpty
export PHOTOC_COMPLETION_DIR=${1:h}
test_root=$(mktemp -d "$2/completion-zsh.XXXXXX")
export PHOTOC_COMPLETION_RESULT="$test_root/matches"
trap 'zpty -d photoc-test 2>/dev/null || true; rm -rf "$test_root"' EXIT
mkdir "$test_root/Photo album"
touch "$test_root/Photo image.JPG" "$test_root/ordinary.txt"

cat > "$test_root/setup.zsh" <<'ZSH'
fpath=("$PHOTOC_COMPLETION_DIR" $fpath)
autoload -Uz compinit
compinit -D -i
PROMPT='completion-test> '
# Capture the matches supplied to the real completion system in a ZLE widget.
compadd() {
    local -a captured
    builtin compadd -A captured "$@"
    photoc_test_matches+=("${captured[@]}")
    builtin compadd "$@"
}
photoc_test_complete() {
    local -a photoc_test_matches
    _main_complete
    print -rl -- "${photoc_test_matches[@]}" > "$PHOTOC_COMPLETION_RESULT"
    zle -M PHOTOC_DONE
}
zle -C photoc-test-widget .complete-word photoc_test_complete
bindkey '^I' photoc-test-widget
print -r -- PHOTOC_READY
ZSH

# zpty expects shell-quoted text when arguments contain spaces.
zpty photoc-test zsh -f
zpty -w photoc-test "cd ${(q)test_root}; source ${(q)test_root}/setup.zsh"
zpty -r photoc-test transcript '*PHOTOC_READY*'
typeset -a replies
query() {
    zpty -w -n photoc-test $'\C-u'"$1"$'\t'
    zpty -r photoc-test transcript '*PHOTOC_DONE*'
    replies=("${(@f)$(<"$PHOTOC_COMPLETION_RESULT")}")
}
has() {
    if (( ${replies[(Ie)$1]} == 0 )); then
        print -u2 -r -- "Missing completion: $1 (got: ${replies[*]})"
        exit 1
    fi
}
lacks() {
    if (( ${replies[(Ie)$1]} != 0 )); then
        print -u2 -r -- "Unexpected completion: $1"
        exit 1
    fi
}

query 'photoc '
for name in compress exif duplicates stats rename sort focus scrub; do has "$name"; done
query 'photoc --ver'
has --version
query 'photoc -v st'
has stats
for name in exif duplicates stats; do
    query "photoc $name --"
    has --json; lacks --version
done
for name in compress rename sort focus scrub; do
    query "photoc $name --"
    lacks --json
done
for name in compress duplicates stats rename sort scrub; do
    query "photoc $name --rec"
    has --recursive
done
query 'photoc rename --'
has --format; has --apply; lacks --gps
query 'photoc scrub --'
has --gps; has --in-place; lacks --apply
query 'photoc compress --'
for flag in --quality --target --min-quality --output-dir; do has "$flag"; done
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
has 'Photo\ album'; lacks 'Photo\ image.JPG'
query 'photoc exif Photo'
has 'Photo\ image.JPG'
query 'photoc compress ./ --output-dir Photo'
has 'Photo\ album'; lacks 'Photo\ image.JPG'
query 'photoc exif -- --'
lacks --help; lacks --json
print -r -- 'zsh completion checks passed.'

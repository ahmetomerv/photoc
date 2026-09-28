#!/bin/sh
# Remove only the unchanged executable and receipt recorded by install.sh.
set -eu
umask 077

usage() {
    cat <<'EOF'
Usage: sh uninstall.sh [--install-dir DIRECTORY] [--dry-run|--apply]

Preview removal of a tracked photoc release installation in $HOME/.local/bin.
  --install-dir DIRECTORY  Use the absolute directory passed to install.sh.
  --dry-run               Preview only (default).
  --apply                 Remove the verified executable and ownership receipt.
  -h, --help              Show this help.

Changed files, symlinks, and installations without a valid receipt are refused.
Configuration, shell startup files, completions, man pages, and the install
directory are preserved. No sudo is used and no downloaded binary is executed.
EOF
}

die() {
    printf 'uninstall: error: %s\n' "$*" >&2
    exit 1
}

usage_error() {
    printf 'uninstall: error: %s\n' "$*" >&2
    printf 'Use --help for uninstall options.\n' >&2
    exit 2
}

install_dir=${HOME:+$HOME/.local/bin}
apply=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --install-dir)
            [ "$#" -ge 2 ] || usage_error "--install-dir requires a value"
            install_dir=$2
            shift 2
            ;;
        --dry-run) apply=0; shift ;;
        --apply) apply=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) usage_error "unknown argument: $1" ;;
    esac
done
case "$install_dir" in
    /*) ;;
    *) die "installation directory must be absolute; set HOME or use --install-dir" ;;
esac

binary=$install_dir/photoc
receipt=$install_dir/.photoc-install-receipt
if [ ! -e "$binary" ] && [ ! -L "$binary" ] &&
   [ ! -e "$receipt" ] && [ ! -L "$receipt" ]; then
    printf 'No tracked photoc installation in %s.\n' "$install_dir"
    exit 0
fi
if [ ! -f "$receipt" ] || [ -L "$receipt" ]; then
    die "no regular ownership receipt in $install_dir; nothing was removed"
fi

for tool in uname awk stat mktemp mv rm rmdir; do
    command -v "$tool" >/dev/null 2>&1 || die "required tool not found: $tool"
done
if command -v sha256sum >/dev/null 2>&1; then
    checksum_tool=sha256sum
elif command -v shasum >/dev/null 2>&1; then
    checksum_tool=shasum
else
    die "verification requires sha256sum or shasum"
fi
os=$(uname -s)
case "$os" in
    Darwin|Linux) ;;
    *) die "unsupported platform: $os" ;;
esac

hash_file() {
    if [ "$checksum_tool" = sha256sum ]; then
        output=$(sha256sum "$1") || return 1
    else
        output=$(shasum -a 256 "$1") || return 1
    fi
    printf '%s\n' "${output%% *}"
}

file_identity() {
    case "$os" in
        Darwin) stat -f '%d:%i' "$1" ;;
        Linux) stat -c '%d:%i' "$1" ;;
    esac
}

# This format contains no paths or shell code, and is never sourced or evaluated.
record=$(awk '
    NR == 1 { if ($0 != "photoc-install-v1") invalid = 1 }
    NR == 2 {
        if (NF != 2 || $1 != "sha256" || length($2) != 64 || $2 ~ /[^0-9a-f]/) invalid = 1
        digest = $2
    }
    NR == 3 {
        if (NF != 2 || $1 != "identity" || $2 !~ /^[0-9]+:[0-9]+$/) invalid = 1
        identity = $2
    }
    END {
        if (NR != 3 || invalid) exit 1
        print digest " " identity
    }
' "$receipt") || die "invalid ownership receipt; nothing was removed"
expected_hash=${record%% *}
expected_identity=${record#* }
receipt_hash=$(hash_file "$receipt") || die "cannot read receipt checksum"
receipt_identity=$(file_identity "$receipt") || die "cannot identify receipt"

verify_binary() {
    [ -f "$1" ] && [ ! -L "$1" ] || return 1
    identity=$(file_identity "$1") || return 1
    [ "$identity" = "$expected_identity" ] || return 1
    digest=$(hash_file "$1") || return 1
    [ "$digest" = "$expected_hash" ]
}

has_binary=0
if [ -e "$binary" ] || [ -L "$binary" ]; then
    verify_binary "$binary" \
        || die "$binary changed or is unsafe; nothing was removed"
    has_binary=1
fi

if [ "$apply" -eq 0 ]; then
    if [ "$has_binary" -eq 1 ]; then printf 'Would remove %s\n' "$binary"; fi
    printf 'Would remove %s\n' "$receipt"
    printf 'Dry run: use --apply to uninstall.\n'
    exit 0
fi

# Move entries into a private directory, then verify again before deleting.
# If another process replaced a checked entry, preserve it here for recovery.
quarantine=$(mktemp -d "$install_dir/.photoc-uninstall.XXXXXX") \
    || die "cannot stage removal in $install_dir"
cleanup() {
    if [ -d "$quarantine" ]; then
        if ! rmdir "$quarantine" 2>/dev/null; then
            printf 'uninstall: preserved files in %s; restore them manually before retrying.\n' "$quarantine" >&2
        fi
    fi
}
trap cleanup 0
trap 'exit 1' HUP INT TERM

if [ "$has_binary" -eq 1 ]; then
    mv "$binary" "$quarantine/photoc" || die "cannot stage executable removal"
fi
mv "$receipt" "$quarantine/.photoc-install-receipt" || die "cannot stage receipt removal"
if [ "$has_binary" -eq 1 ]; then
    verify_binary "$quarantine/photoc" \
        || die "executable changed during uninstall; staged files were preserved"
fi
staged_receipt=$quarantine/.photoc-install-receipt
if [ ! -f "$staged_receipt" ] || [ -L "$staged_receipt" ]; then
    die "receipt changed during uninstall; staged files were preserved"
fi
staged_identity=$(file_identity "$staged_receipt") || die "cannot identify staged receipt"
staged_hash=$(hash_file "$staged_receipt") || die "cannot read staged receipt"
if [ "$staged_identity" != "$receipt_identity" ] || [ "$staged_hash" != "$receipt_hash" ]; then
    die "receipt changed during uninstall; staged files were preserved"
fi

if [ "$has_binary" -eq 1 ]; then
    rm -- "$quarantine/photoc" || die "cannot remove staged executable"
    printf 'Removed %s\n' "$binary"
fi
rm -- "$staged_receipt" || die "cannot remove staged receipt"
printf 'Removed %s\n' "$receipt"

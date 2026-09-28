#!/bin/sh
# Install a checksum-verified executable from a public photoc GitHub release.
set -eu
umask 077

usage() {
    cat <<'EOF'
Usage: sh install.sh [--version TAG] [--install-dir DIRECTORY]

Install photoc for macOS arm64, macOS x86_64, or Linux x86_64.
The default is the latest release, installed to $HOME/.local/bin.

  --version TAG           Install a specific release tag (for example v0.1.0).
  --install-dir DIRECTORY  Use an absolute directory instead of $HOME/.local/bin.
  -h, --help              Show this help.

The SHA-256 checksum is verified before the executable is run or installed.
An ownership receipt is recorded for scripts/uninstall.sh.
Existing files and symlinks are never replaced. No sudo is used.
EOF
}

die() {
    printf 'install: error: %s\n' "$*" >&2
    exit 1
}

usage_error() {
    printf 'install: error: %s\n' "$*" >&2
    printf 'Use --help for installer options.\n' >&2
    exit 2
}

version=latest
install_dir=${HOME:+$HOME/.local/bin}
while [ "$#" -gt 0 ]; do
    case "$1" in
        --version|--install-dir)
            option=$1
            [ "$#" -ge 2 ] || usage_error "$option requires a value"
            case "$option" in
                --version) version=$2 ;;
                --install-dir) install_dir=$2 ;;
            esac
            shift 2
            ;;
        -h|--help) usage; exit 0 ;;
        *) usage_error "unknown argument: $1" ;;
    esac
done

validate_tag() {
    case "$1" in
        ''|*[!a-zA-Z0-9._-]*|.*|-*) die "invalid release tag: $1" ;;
    esac
}
validate_tag "$version"
case "$install_dir" in
    /*) ;;
    *) die "installation directory must be absolute; set HOME or use --install-dir" ;;
esac
case "$install_dir" in
    *:*) die "installation directory cannot contain ':' (the PATH separator)" ;;
esac

for tool in uname curl mktemp mkdir cp chmod ln rm awk sed stat; do
    command -v "$tool" >/dev/null 2>&1 || die "required tool not found: $tool"
done
if command -v sha256sum >/dev/null 2>&1; then
    checksum_tool=sha256sum
elif command -v shasum >/dev/null 2>&1; then
    checksum_tool=shasum
else
    die "SHA-256 verification requires sha256sum or shasum"
fi

os=$(uname -s)
arch=$(uname -m)
case "$os:$arch" in
    Darwin:arm64|Darwin:aarch64) platform=darwin-arm64 ;;
    Darwin:x86_64|Darwin:amd64) platform=darwin-x86_64 ;;
    Linux:x86_64|Linux:amd64) platform=linux-x86_64 ;;
    *) die "unsupported platform: $os $arch; build from source instead" ;;
esac

destination=$install_dir/photoc
receipt=$install_dir/.photoc-install-receipt
if [ -e "$destination" ] || [ -L "$destination" ]; then
    die "$destination already exists; preview scripts/uninstall.sh or move an untracked file aside"
fi
if [ -e "$receipt" ] || [ -L "$receipt" ]; then
    die "$receipt already exists; use scripts/uninstall.sh to inspect the tracked installation"
fi

work_dir=
stage_dir=
cleanup() {
    # Roll back our receipt if publishing the binary failed. Keep receipts for
    # completed installs, including a signal just after the binary was linked.
    # shellcheck disable=SC3013
    # -ef is supported by macOS /bin/sh and Linux dash/bash; older ShellCheck
    # versions predate its inclusion in POSIX.1-2024. Keep the inode check.
    if [ -n "$stage_dir" ] && [ ! -L "$receipt" ] &&
       [ "$receipt" -ef "$stage_dir/.photoc-install-receipt" ] &&
       ! [ "$destination" -ef "$stage_dir/photoc" ]; then
        rm -f -- "$receipt"
    fi
    if [ -n "$stage_dir" ]; then rm -rf -- "$stage_dir"; fi
    if [ -n "$work_dir" ]; then rm -rf -- "$work_dir"; fi
}
trap cleanup 0
trap 'exit 1' HUP INT TERM
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/photoc-download.XXXXXX") || die "cannot create download directory"

# Disable curlrc overrides and permit HTTPS for both requests and redirects.
download() {
    curl --disable --fail --silent --show-error --location \
        --proto '=https' --proto-redir '=https' \
        --connect-timeout 15 --max-time 300 --retry 2 "$@"
}

repository=https://github.com/ahmetomerv/photoc
if [ "$version" = latest ]; then
    release_url=$(download --output /dev/null --write-out '%{url_effective}' \
        "$repository/releases/latest") || die "cannot resolve the latest release; a public release must be published first"
    case "$release_url" in
        "$repository/releases/tag/"*) version=${release_url#"$repository/releases/tag/"} ;;
        *) die "GitHub did not redirect to a release tag" ;;
    esac
    validate_tag "$version"
fi

artifact=photoc-$platform
release_base=$repository/releases/download/$version
printf 'Downloading photoc %s for %s...\n' "$version" "$platform"
download --output "$work_dir/SHA256SUMS" "$release_base/SHA256SUMS" \
    || die "cannot download SHA256SUMS for $version; check the release assets"
download --output "$work_dir/photoc" "$release_base/$artifact" \
    || die "cannot download $artifact for $version; check the release assets"

# Accept the standard sha256sum/shasum format, with exactly one matching entry.
expected=$(awk -v asset="$artifact" '
    { sub(/\r$/, "") }
    NF == 2 && ($2 == asset || $2 == "*" asset) {
        count++
        digest = tolower($1)
        if (length(digest) != 64 || digest ~ /[^0-9a-f]/) invalid = 1
    }
    END {
        if (count != 1 || invalid) exit 1
        print digest
    }
' "$work_dir/SHA256SUMS") || die "missing, duplicate, or invalid checksum for $artifact"
if [ "$checksum_tool" = sha256sum ]; then
    hash_output=$(sha256sum "$work_dir/photoc") || die "cannot calculate SHA-256"
else
    hash_output=$(shasum -a 256 "$work_dir/photoc") || die "cannot calculate SHA-256"
fi
actual=${hash_output%% *}
[ "$actual" = "$expected" ] || die "checksum mismatch for $artifact; nothing was installed"

mkdir -p "$install_dir" || die "cannot create $install_dir; choose a writable --install-dir"
stage_dir=$(mktemp -d "$install_dir/.photoc-install.XXXXXX") \
    || die "cannot stage installation in $install_dir; choose a writable --install-dir"
cp "$work_dir/photoc" "$stage_dir/photoc" || die "cannot stage executable"
chmod 755 "$stage_dir/photoc" || die "cannot set executable permissions"
binary_version=$("$stage_dir/photoc" --version) \
    || die "downloaded binary cannot run; check platform and runtime libraries"
case "$binary_version" in
    'photoc '[0-9]*) ;;
    *) die "downloaded executable did not report a photoc version" ;;
esac
[ "$binary_version" = "photoc ${version#v}" ] \
    || die "downloaded executable version does not match release $version; nothing was installed"

if [ "$checksum_tool" = sha256sum ]; then
    staged_hash=$(sha256sum "$stage_dir/photoc") || die "cannot verify staged executable"
else
    staged_hash=$(shasum -a 256 "$stage_dir/photoc") || die "cannot verify staged executable"
fi
[ "${staged_hash%% *}" = "$expected" ] || die "staged executable changed; nothing was installed"
case "$os" in
    Darwin) identity=$(stat -f '%d:%i' "$stage_dir/photoc") ;;
    Linux) identity=$(stat -c '%d:%i' "$stage_dir/photoc") ;;
esac
printf 'photoc-install-v1\nsha256 %s\nidentity %s\n' "$expected" "$identity" \
    > "$stage_dir/.photoc-install-receipt"
ln "$stage_dir/.photoc-install-receipt" "$install_dir" \
    || die "cannot record installation receipt in $install_dir"

# Linking a staged file named photoc into the directory atomically creates the
# final name without replacing any existing file, directory, or symlink.
ln "$stage_dir/photoc" "$install_dir" \
    || die "cannot install $destination; an existing entry or permissions may block it"
printf 'Installed %s to %s\n' "$binary_version" "$destination"
printf 'Recorded install receipt at %s\n' "$receipt"

case ":${PATH:-}:" in
    *":$install_dir:"*) ;;
    *)
        quoted_dir=$(printf '%s' "$install_dir" | sed "s/'/'\\\\''/g")
        printf 'Add this line to your shell startup file (for example ~/.zshrc or ~/.bashrc):\n'
        printf "  export PATH='%s':\"\$PATH\"\n" "$quoted_dir"
        printf 'For fish, use: fish_add_path '\''%s'\''\n' "$quoted_dir"
        ;;
esac

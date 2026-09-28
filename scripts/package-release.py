#!/usr/bin/env python3
"""Package tested native binaries and verify release assets; standard library only."""

import argparse
import gzip
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile


ROOT = Path(__file__).resolve().parents[1]
PLATFORMS = ("darwin-arm64", "darwin-x86_64", "linux-x86_64")


def project_version():
    version = (ROOT / "VERSION").read_text(encoding="ascii").strip()
    if not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version):
        raise ValueError("VERSION must contain MAJOR.MINOR.PATCH without leading zeroes")
    return version


def release_version(tag):
    version = project_version()
    if tag != "v" + version:
        raise ValueError(f"release tag {tag!r} does not match VERSION; expected 'v{version}'")
    return version


def asset_names(tag, platform):
    version = release_version(tag)
    if platform not in PLATFORMS:
        raise ValueError(f"unsupported platform: {platform}")
    return (f"photoc-{platform}", f"photoc-{version}-{platform}.tar.gz",
            f"SHA256SUMS-{platform}")


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def check_binary(binary, version):
    result = subprocess.run([str(binary.resolve()), "--version"], check=True,
                            capture_output=True, text=True, timeout=15)
    if result.stdout != f"photoc {version}\n" or result.stderr:
        raise ValueError(f"release tag does not match binary version: {result.stdout.strip()!r}")


def package(tag, platform, binary, output):
    version = release_version(tag)
    raw_name, archive_name, manifest_name = asset_names(tag, platform)
    check_binary(binary, version)
    output.mkdir(parents=True, exist_ok=True)
    for name in (raw_name, archive_name, manifest_name):
        if (output / name).exists() or (output / name).is_symlink():
            raise ValueError(f"refusing to replace existing asset: {name}")

    with tempfile.TemporaryDirectory(prefix=".photoc-package-", dir=output) as temporary:
        stage = Path(temporary)
        bundle = stage / archive_name.removesuffix(".tar.gz")
        (bundle / "bin").mkdir(parents=True)
        shutil.copyfile(binary, bundle / "bin/photoc")
        (bundle / "bin/photoc").chmod(0o755)
        man = bundle / "share/man/man1/photoc.1"
        man.parent.mkdir(parents=True)
        shutil.copyfile(ROOT / "man/photoc.1", man)
        for name in ("VERSION", "LICENSE", "README.md", "CONTRIBUTING.md", "AGENTS.md"):
            shutil.copyfile(ROOT / name, bundle / name)
        for name in ("docs", "completions"):
            shutil.copytree(ROOT / name, bundle / name)
        (bundle / "scripts").mkdir()
        for name in ("install.sh", "uninstall.sh"):
            shutil.copyfile(ROOT / "scripts" / name, bundle / "scripts" / name)

        def archive_metadata(member):
            if not member.isdir() and not member.isfile():
                raise ValueError(f"unexpected non-regular archive entry: {member.name}")
            member.uid = member.gid = member.mtime = 0
            member.uname = member.gname = ""
            executable = member.name in (
                f"{bundle.name}/bin/photoc", f"{bundle.name}/scripts/install.sh",
                f"{bundle.name}/scripts/uninstall.sh")
            member.mode = 0o755 if member.isdir() or executable else 0o644
            return member

        with (stage / archive_name).open("wb") as stream:
            with gzip.GzipFile(filename="", fileobj=stream, mode="wb", mtime=0) as compressed:
                with tarfile.open(fileobj=compressed, mode="w", format=tarfile.USTAR_FORMAT) as archive:
                    archive.add(bundle, arcname=bundle.name, filter=archive_metadata)
        shutil.copyfile(bundle / "bin/photoc", stage / raw_name)
        (stage / raw_name).chmod(0o755)
        manifest = "".join(f"{sha256(stage / name)}  {name}\n"
                           for name in sorted((raw_name, archive_name)))
        (stage / manifest_name).write_text(manifest, encoding="ascii")
        # Exclusive creation preserves any assets another process created.
        for name in (raw_name, archive_name, manifest_name):
            with (output / name).open("xb") as destination:
                with (stage / name).open("rb") as source:
                    shutil.copyfileobj(source, destination)
            (output / name).chmod(0o755 if name == raw_name else 0o644)
    print(f"Packaged {archive_name} and {raw_name}")


def collect_checksums(tag, directory):
    release_version(tag)
    entries = {}
    for platform in PLATFORMS:
        raw, archive, manifest = asset_names(tag, platform)
        lines = (directory / manifest).read_text(encoding="ascii").splitlines()
        platform_entries = {}
        for line in lines:
            match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
            if not match or match[2] not in (raw, archive) or match[2] in platform_entries:
                raise ValueError(f"invalid or duplicate checksum entry in {manifest}")
            platform_entries[match[2]] = match[1]
        if set(platform_entries) != {raw, archive}:
            raise ValueError(f"incomplete checksums in {manifest}")
        for name, expected in platform_entries.items():
            path = directory / name
            if path.is_symlink() or not path.is_file() or sha256(path) != expected:
                raise ValueError(f"checksum verification failed: {name}")
            entries[name] = expected
    with (directory / "SHA256SUMS").open("x", encoding="ascii") as manifest:
        for name in sorted(entries):
            manifest.write(f"{entries[name]}  {name}\n")
    print("Verified all three platforms and wrote SHA256SUMS")


def homebrew_formula(source_archive, output):
    version = project_version()
    # Read only the top-level version and source entries; never extract an archive.
    with tarfile.open(source_archive, "r:gz") as archive:
        versions = [member for member in archive.getmembers()
                    if member.isfile() and len(member.name.split("/")) == 2
                    and member.name.endswith("/VERSION")]
        if len(versions) != 1:
            raise ValueError("source archive must contain one top-level VERSION file")
        member = versions[0]
        archived_version = archive.extractfile(member).read(128).decode("ascii").strip()
        if archived_version != version:
            raise ValueError(f"source archive VERSION does not match {version}")
        root = member.name.split("/")[0]
        required = ("CMakeLists.txt", "cmake/Version.cmake", "src/main.c",
                    "man/photoc.1", "completions/bash/photoc", "completions/zsh/_photoc",
                    "completions/fish/photoc.fish")
        for name in required:
            try:
                present = archive.getmember(root + "/" + name).isfile()
            except KeyError:
                present = False
            if not present:
                raise ValueError(f"source archive is missing {name}")
    template = (ROOT / "packaging/homebrew/photoc.rb.in").read_text(encoding="utf-8")
    formula = template.replace("@VERSION@", version).replace("@SHA256@", sha256(source_archive))
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8") as stream:
        stream.write(formula)
    print(f"Wrote {output} for v{version}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    version = commands.add_parser("version", help="read VERSION and optionally validate a release tag")
    version.add_argument("--tag")
    packaging = commands.add_parser("package", help="package a tested native executable")
    packaging.add_argument("--tag", required=True)
    packaging.add_argument("--platform", choices=PLATFORMS, required=True)
    packaging.add_argument("--binary", type=Path, default=Path("build/photoc"))
    packaging.add_argument("--output", type=Path, default=Path("dist"))
    checksums = commands.add_parser("checksums", help="verify all platforms and combine checksums")
    checksums.add_argument("--tag", required=True)
    checksums.add_argument("--directory", type=Path, default=Path("dist"))
    homebrew = commands.add_parser("homebrew", help="generate a tap formula from tagged source")
    homebrew.add_argument("--source-archive", type=Path, required=True)
    homebrew.add_argument("--output", type=Path, default=Path("dist/homebrew/photoc.rb"))
    args = parser.parse_args()
    try:
        if args.command == "version":
            print(release_version(args.tag) if args.tag is not None else project_version())
        elif args.command == "package":
            package(args.tag, args.platform, args.binary, args.output)
        elif args.command == "checksums":
            collect_checksums(args.tag, args.directory)
        else:
            homebrew_formula(args.source_archive, args.output)
    except (OSError, ValueError, subprocess.SubprocessError, tarfile.TarError) as error:
        parser.exit(1, f"release: error: {error}\n")


if __name__ == "__main__":
    main()

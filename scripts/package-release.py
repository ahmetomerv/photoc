#!/usr/bin/env python3
"""Package tested native binaries and verify release assets; standard library only."""

import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile


ROOT = Path(__file__).resolve().parents[1]
PLATFORMS = ("darwin-arm64", "darwin-x86_64", "linux-x86_64")
PKG_CONFIG_MODULES = ("libexif", "libturbojpeg", "libjpeg", "libxml-2.0")
LINUX_RELEASE_PACKAGES = (
    "build-essential", "cmake", "pkg-config", "libexif-dev", "libturbojpeg0-dev",
    "libjpeg*-dev", "libxml2-dev", "python3", "zsh", "fish", "mandoc",
)
MACOS_RELEASE_PACKAGES = ("cmake", "pkgconf", "libexif", "jpeg-turbo", "libxml2", "fish")


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


def bump_version(part, apply=False):
    """Return the next version; write only VERSION when explicitly applied."""
    if part not in ("major", "minor", "patch"):
        raise ValueError(f"unsupported version bump: {part}")
    major, minor, patch = map(int, project_version().split("."))
    if part == "major":
        major, minor, patch = major + 1, 0, 0
    elif part == "minor":
        minor, patch = minor + 1, 0
    else:
        patch += 1
    version = f"{major}.{minor}.{patch}"
    if apply:
        (ROOT / "VERSION").write_text(version + "\n", encoding="ascii")
    return version


def asset_names(tag, platform):
    version = release_version(tag)
    if platform not in PLATFORMS:
        raise ValueError(f"unsupported platform: {platform}")
    return (f"photoc-{platform}", f"photoc-{version}-{platform}.tar.gz",
            f"SHA256SUMS-{platform}")


def metadata_names(tag, platform):
    version = release_version(tag)
    if platform not in PLATFORMS:
        raise ValueError(f"unsupported platform: {platform}")
    stem = f"photoc-{version}-{platform}"
    return (f"{stem}.dependencies.json", f"{stem}.spdx.json")


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _command_output(command):
    return subprocess.run(command, check=True, capture_output=True, text=True,
                          timeout=30).stdout


def workflow_actions(workflow):
    actions = {}
    pattern = re.compile(r"^\s*(?:-\s*)?uses:\s*([^@\s]+)@([^#\s]+)(?:\s+#\s*(\S+))?\s*$")
    for line in workflow.read_text(encoding="utf-8").splitlines():
        match = pattern.match(line)
        if not match:
            continue
        name, commit, version = match.groups()
        if not re.fullmatch(r"[0-9a-f]{40}", commit):
            raise ValueError(f"workflow action {name} must use an immutable commit SHA")
        record = {"name": name, "commit": commit}
        if version:
            record["version"] = version
        previous = actions.get((name, commit))
        if previous is not None and previous != record:
            raise ValueError(f"inconsistent version comments for workflow action {name}")
        actions[(name, commit)] = record
    return sorted(actions.values(), key=lambda record: (record["name"], record["commit"]))


def _cmake_value(path, name):
    pattern = re.compile(rf'^set\({re.escape(name)} "([^"]*)"\)$', re.MULTILINE)
    match = pattern.search(path.read_text(encoding="utf-8"))
    if not match:
        raise ValueError(f"missing {name} in {path}")
    return match.group(1)


def _cmake_cache_value(path, name):
    pattern = re.compile(rf"^{re.escape(name)}:[^=]+=(.*)$", re.MULTILINE)
    match = pattern.search(path.read_text(encoding="utf-8"))
    if not match:
        raise ValueError(f"missing {name} in {path}")
    return match.group(1)


def _pkg_config_dependencies():
    dependencies = []
    for module in PKG_CONFIG_MODULES:
        record = {
            "module": module,
            "version": _command_output(["pkg-config", "--modversion", module]).strip(),
        }
        for variable in ("prefix", "libdir", "includedir", "pcfiledir"):
            record[variable] = _command_output(
                ["pkg-config", f"--variable={variable}", module]
            ).strip()
        dependencies.append(record)
    return dependencies


def _system_packages(platform):
    if platform.startswith("linux-"):
        output = _command_output([
            "dpkg-query", "-W", "-f=${binary:Package}\\t${Version}\\t${Architecture}\\n",
            *LINUX_RELEASE_PACKAGES,
        ])
        packages = []
        for line in output.splitlines():
            name, version, architecture = line.split("\t")
            packages.append({"name": name, "version": version, "architecture": architecture})
        return "dpkg", sorted(packages, key=lambda package: package["name"])
    if platform.startswith("darwin-"):
        output = _command_output(["brew", "list", "--versions", *MACOS_RELEASE_PACKAGES])
        packages = []
        for line in output.splitlines():
            name, *versions = line.split()
            if not versions:
                raise ValueError(f"missing Homebrew version for {name}")
            packages.append({"name": name, "version": " ".join(versions)})
        return "homebrew", sorted(packages, key=lambda package: package["name"])
    raise ValueError(f"unsupported platform: {platform}")


def _linked_libraries(platform, binary):
    if platform.startswith("linux-"):
        output = _command_output(["ldd", str(binary)])
        libraries = []
        for line in output.splitlines():
            line = line.strip()
            match = re.match(r"(\S+)\s+=>\s+(\S+)(?:\s+\(.*\))?$", line)
            if match:
                libraries.append({"name": match[1], "path": match[2]})
                continue
            match = re.match(r"(/\S+)(?:\s+\(.*\))?$", line)
            if match:
                libraries.append({"name": Path(match[1]).name, "path": match[1]})
        return libraries
    if platform.startswith("darwin-"):
        output = _command_output(["otool", "-L", str(binary)])
        libraries = []
        for line in output.splitlines()[1:]:
            path = line.strip().split(" (", 1)[0]
            if path:
                libraries.append({"name": Path(path).name, "path": path})
        return libraries
    raise ValueError(f"unsupported platform: {platform}")


def write_dependency_manifest(tag, platform, binary, build_directory, workflow, output,
                              environment=None):
    release_version(tag)
    if platform not in PLATFORMS:
        raise ValueError(f"unsupported platform: {platform}")
    environment = dict(os.environ if environment is None else environment)
    required_environment = (
        "GITHUB_REPOSITORY", "GITHUB_SHA", "GITHUB_REF", "GITHUB_WORKFLOW_REF",
        "GITHUB_WORKFLOW_SHA", "GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT", "RUNNER_OS",
        "RUNNER_ARCH",
    )
    missing = [name for name in required_environment if not environment.get(name)]
    if missing:
        raise ValueError("missing release environment: " + ", ".join(missing))
    for name in ("GITHUB_SHA", "GITHUB_WORKFLOW_SHA"):
        if not re.fullmatch(r"[0-9a-f]{40}", environment[name]):
            raise ValueError(f"{name} must be a full commit SHA")
    if output.exists() or output.is_symlink():
        raise ValueError(f"refusing to replace existing manifest: {output}")
    if binary.is_symlink() or not binary.is_file():
        raise ValueError(f"release binary is not a regular file: {binary}")

    compiler_files = list(build_directory.glob("CMakeFiles/*/CMakeCCompiler.cmake"))
    if len(compiler_files) != 1:
        raise ValueError("expected exactly one configured C compiler description")
    compiler_file = compiler_files[0]
    compiler_path = _cmake_value(compiler_file, "CMAKE_C_COMPILER")
    cmake_path = _cmake_cache_value(build_directory / "CMakeCache.txt", "CMAKE_COMMAND")
    package_manager, packages = _system_packages(platform)
    manifest = {
        "schemaVersion": 1,
        "release": {"tag": tag, "platform": platform},
        "source": {
            "repository": environment["GITHUB_REPOSITORY"],
            "commit": environment["GITHUB_SHA"],
            "ref": environment["GITHUB_REF"],
        },
        "build": {
            "workflow": {
                "ref": environment["GITHUB_WORKFLOW_REF"],
                "commit": environment["GITHUB_WORKFLOW_SHA"],
                "runId": environment["GITHUB_RUN_ID"],
                "runAttempt": environment["GITHUB_RUN_ATTEMPT"],
                "actions": workflow_actions(workflow),
            },
            "runner": {
                "os": environment["RUNNER_OS"],
                "architecture": environment["RUNNER_ARCH"],
                "imageOS": environment.get("ImageOS"),
                "imageVersion": environment.get("ImageVersion"),
            },
            "binary": {"sha256": sha256(binary)},
        },
        "toolchain": {
            "compiler": {
                "path": compiler_path,
                "id": _cmake_value(compiler_file, "CMAKE_C_COMPILER_ID"),
                "version": _cmake_value(compiler_file, "CMAKE_C_COMPILER_VERSION"),
                "details": _command_output([compiler_path, "--version"]).strip(),
            },
            "cmake": {
                "path": cmake_path,
                "version": _command_output([cmake_path, "--version"]).strip(),
            },
            "pkgConfig": {"version": _command_output(["pkg-config", "--version"]).strip()},
        },
        "dependencies": {
            "packageManager": package_manager,
            "packages": packages,
            "pkgConfig": _pkg_config_dependencies(),
            "linkedLibraries": _linked_libraries(platform, binary),
        },
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8") as stream:
        json.dump(manifest, stream, indent=2, sort_keys=True)
        stream.write("\n")


def check_binary(binary, version):
    result = subprocess.run([str(binary.resolve()), "--version"], check=True,
                            capture_output=True, text=True, timeout=15)
    if result.stdout != f"photoc {version}\n" or result.stderr:
        raise ValueError(f"release tag does not match binary version: {result.stdout.strip()!r}")


def _read_json_file(path, description):
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"{description} is not a regular file: {path}")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid {description}: {path}") from error
    if not isinstance(value, dict):
        raise ValueError(f"invalid {description}: expected a JSON object")
    return value


def package(tag, platform, binary, output, dependency_manifest=None, sbom=None):
    version = release_version(tag)
    raw_name, archive_name, manifest_name = asset_names(tag, platform)
    dependency_name, sbom_name = metadata_names(tag, platform)
    if (dependency_manifest is None) != (sbom is None):
        raise ValueError("dependency manifest and SBOM must be provided together")
    metadata = dependency_manifest is not None
    if metadata:
        dependency_data = _read_json_file(dependency_manifest, "dependency manifest")
        if dependency_data.get("schemaVersion") != 1 or dependency_data.get("release") != {
                "tag": tag, "platform": platform}:
            raise ValueError("dependency manifest does not match the release tag and platform")
        sbom_data = _read_json_file(sbom, "SBOM")
        if (sbom_data.get("spdxVersion") != "SPDX-2.3" or
                sbom_data.get("SPDXID") != "SPDXRef-DOCUMENT"):
            raise ValueError("SBOM must be an SPDX 2.3 JSON document")
    check_binary(binary, version)
    output.mkdir(parents=True, exist_ok=True)
    output_names = [raw_name, archive_name, manifest_name]
    if metadata:
        output_names.extend((dependency_name, sbom_name))
    for name in output_names:
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
        for name in ("VERSION", "LICENSE", "THIRD_PARTY_NOTICES.md", "README.md",
                     "CONTRIBUTING.md", "AGENTS.md", "CODE_OF_CONDUCT.md", "SECURITY.md"):
            shutil.copyfile(ROOT / name, bundle / name)
        for name in ("assets", "docs", "completions", "licenses"):
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
        checksum_names = [raw_name, archive_name]
        if metadata:
            shutil.copyfile(dependency_manifest, stage / dependency_name)
            shutil.copyfile(sbom, stage / sbom_name)
            checksum_names.extend((dependency_name, sbom_name))
        manifest = "".join(f"{sha256(stage / name)}  {name}\n"
                           for name in sorted(checksum_names))
        (stage / manifest_name).write_text(manifest, encoding="ascii")
        # Exclusive creation preserves any assets another process created.
        for name in output_names:
            with (output / name).open("xb") as destination:
                with (stage / name).open("rb") as source:
                    shutil.copyfileobj(source, destination)
            (output / name).chmod(0o755 if name == raw_name else 0o644)
    print(f"Packaged {archive_name} and {raw_name}")


def collect_checksums(tag, directory, require_metadata=False):
    release_version(tag)
    entries = {}
    for platform in PLATFORMS:
        raw, archive, manifest = asset_names(tag, platform)
        dependency_name, sbom_name = metadata_names(tag, platform)
        base_names = {raw, archive}
        metadata_asset_names = {dependency_name, sbom_name}
        lines = (directory / manifest).read_text(encoding="ascii").splitlines()
        platform_entries = {}
        for line in lines:
            match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
            if (not match or match[2] not in base_names | metadata_asset_names or
                    match[2] in platform_entries):
                raise ValueError(f"invalid or duplicate checksum entry in {manifest}")
            platform_entries[match[2]] = match[1]
        expected_names = base_names
        present_metadata = set(platform_entries) & metadata_asset_names
        if require_metadata or present_metadata:
            expected_names = base_names | metadata_asset_names
        if set(platform_entries) != expected_names:
            if require_metadata:
                raise ValueError(f"dependency manifest and SBOM required in {manifest}")
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    version = commands.add_parser("version", help="read VERSION and optionally validate a release tag")
    version.add_argument("--tag")
    bump = commands.add_parser("bump", help="preview the next version; use --apply to update VERSION")
    bump.add_argument("part", choices=("major", "minor", "patch"))
    bump.add_argument("--apply", action="store_true", help="write the new version to VERSION")
    packaging = commands.add_parser("package", help="package a tested native executable")
    packaging.add_argument("--tag", required=True)
    packaging.add_argument("--platform", choices=PLATFORMS, required=True)
    packaging.add_argument("--binary", type=Path, default=Path("build/photoc"))
    packaging.add_argument("--output", type=Path, default=Path("dist"))
    packaging.add_argument("--dependency-manifest", type=Path)
    packaging.add_argument("--sbom", type=Path)
    provenance = commands.add_parser(
        "manifest", help="record the release toolchain and resolved native dependencies"
    )
    provenance.add_argument("--tag", required=True)
    provenance.add_argument("--platform", choices=PLATFORMS, required=True)
    provenance.add_argument("--binary", type=Path, default=Path("build/photoc"))
    provenance.add_argument("--build-directory", type=Path, default=Path("build"))
    provenance.add_argument("--workflow", type=Path, default=ROOT / ".github/workflows/release.yml")
    provenance.add_argument("--output", type=Path, required=True)
    checksums = commands.add_parser("checksums", help="verify all platforms and combine checksums")
    checksums.add_argument("--tag", required=True)
    checksums.add_argument("--directory", type=Path, default=Path("dist"))
    checksums.add_argument(
        "--require-metadata", action="store_true",
        help="require a dependency manifest and SBOM for every platform",
    )
    args = parser.parse_args()
    try:
        if args.command == "version":
            print(release_version(args.tag) if args.tag is not None else project_version())
        elif args.command == "bump":
            print(bump_version(args.part, args.apply))
        elif args.command == "package":
            package(
                args.tag, args.platform, args.binary, args.output,
                dependency_manifest=args.dependency_manifest, sbom=args.sbom,
            )
        elif args.command == "manifest":
            write_dependency_manifest(
                args.tag, args.platform, args.binary, args.build_directory,
                args.workflow, args.output,
            )
            print(args.output.read_text(encoding="utf-8"), end="")
        elif args.command == "checksums":
            collect_checksums(args.tag, args.directory, args.require_metadata)
    except (OSError, ValueError, subprocess.SubprocessError, tarfile.TarError) as error:
        parser.exit(1, f"release: error: {error}\n")


if __name__ == "__main__":
    main()

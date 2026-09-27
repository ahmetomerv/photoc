# photoc

`photoc` is a command-line toolkit for photographers, written in C. It can
inspect metadata in JPEG files; the other planned commands are placeholders.

## Build and test

Requires a C17 compiler, CMake 3.21 or newer, a `pkg-config` implementation,
and the libexif development package:

```sh
# macOS (Homebrew)
brew install cmake pkgconf libexif

# Debian/Ubuntu
sudo apt install cmake pkg-config libexif-dev

# Fedora
sudo dnf install cmake pkgconf-pkg-config libexif-devel
```

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

You can run the same steps with `sh scripts/build-and-test.sh`.

## CLI

```sh
./build/photoc --help
./build/photoc --version
./build/photoc exif --help
./build/photoc exif photo.jpg
```

`exif <file>` prints file, image, camera, exposure, date, and location details
for one JPEG. Unavailable EXIF fields are labeled `Unavailable`. The planned
`compress`, `duplicates`, `stats`, `rename`, `sort`, `focus`, and `scrub`
commands still report that they are not implemented.

Global options are `-h`/`--help`, `--version` (also `-V`), `-v`/`--verbose`, `-q`/`--quiet`, and `--json`. Options can appear before or after a command. `--` ends option parsing. Verbose and quiet cannot be combined; they do not alter `exif` output yet. JSON output is not supported by `exif` yet; requesting it returns a usage error.

Exit status is `0` for success, `1` for file or metadata errors, `2` for usage
errors, and `3` for unimplemented commands. Normal output uses stdout;
diagnostics use stderr.

## Shared filesystem utilities

[`include/photoc/fs.h`](include/photoc/fs.h) defines the filesystem API used by future commands. It covers path inspection, filename and extension extraction, safe path joining, and callback-based directory walks. [`include/photoc/photo.h`](include/photoc/photo.h) defines the shared `Photo` metadata model. Both headers document ownership and unavailable values.

`photo_load_metadata` loads JPEG dimensions, file size, and available EXIF fields
into a `Photo`. It reports distinct results for unsupported extensions, invalid
JPEG data, filesystem errors, and allocation failures. Missing EXIF fields are
left unavailable. Call `photo_cleanup` after a successful load.

## EXIF dependency

[`libexif`](https://libexif.github.io/) is a C library for reading, editing,
and serializing EXIF metadata. CMake finds the system installation through its
`libexif.pc` file; the library is not vendored. It is LGPL-licensed and remains
separate from photoc's MIT-licensed source. Binary distributors must meet the
LGPL requirements for the library they ship or link.

libexif's direct file-loading API targets JPEG. Its save API produces an EXIF
data buffer, so safely writing modified metadata back into an image file will
need additional work. RAW and HEIC support are outside this integration.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). The project is licensed under the [MIT License](LICENSE).

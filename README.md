# photoc

`photoc` is a command-line toolkit for photographers, written in C. This initial project skeleton provides the CLI structure only; it does not process photos yet.

## Build and test

Requires a C17 compiler and CMake 3.21 or newer.

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
./build/photoc compress --help
```

The planned commands are `compress`, `exif`, `duplicates`, `stats`, `rename`, `sort`, `focus`, and `scrub`. Each has command help, but running it currently reports that it is not implemented and exits with status 3.

Global options are `-h`/`--help`, `--version` (also `-V`), `-v`/`--verbose`, `-q`/`--quiet`, and `--json`. Options can appear before or after a command. `--` ends option parsing. Verbose, quiet, and JSON output modes are accepted for future command output; they do not change the current placeholders. Verbose and quiet cannot be combined.

Missing or unknown commands, unknown options, and conflicting options exit with status 2. Normal output uses stdout; diagnostics use stderr.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). The project is licensed under the [MIT License](LICENSE).

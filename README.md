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
```

The planned commands are `compress`, `exif`, `duplicates`, `stats`, `rename`, `sort`, `focus`, and `scrub`. Each currently reports that it is not implemented and exits with status 3. Missing or unknown commands exit with status 2.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). The project is licensed under the [MIT License](LICENSE).

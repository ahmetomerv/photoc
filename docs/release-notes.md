## Downloads and requirements

Choose the archive or executable matching your OS and CPU:

- **macOS arm64 or x86_64:** macOS 15 or newer. Install runtime libraries with
  `brew install libexif jpeg-turbo` on the matching architecture.
- **Linux x86_64:** built on Ubuntu 22.04; requires glibc 2.35 or newer,
  libexif (`libexif.so.12`), and TurboJPEG (`libturbojpeg.so.0`). On Ubuntu,
  install them with `sudo apt install libexif12 libturbojpeg`.

Dependency libraries are dynamically linked and are not bundled. macOS
binaries are not developer-signed or notarized. Other CPUs and Linux systems
without glibc should build from source.

Archives contain `bin/photoc`, its man page, shell completions, documentation,
and the MIT license. `SHA256SUMS` covers all archives and raw executables;
`SHA256SUMS-<platform>` covers that platform's archive and executable. Verify
downloaded files with `shasum -a 256 -c SHA256SUMS-<platform>` (macOS) or
`sha256sum -c SHA256SUMS-<platform>` (Linux), with both files in that directory.

See the [installation guide](https://github.com/ahmetomerv/photoc/blob/main/docs/installation.md)
for the checksum-verified release installer and safe uninstall flow.

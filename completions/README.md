# Shell completions

[Project README](../README.md)

The scripts complete all eight command names, global flags, applicable command
options, and file/directory paths. They also suggest common values for quality,
target size, session gap, and `sort --by date|session`. Suggestions are examples,
not a restriction on valid values. `--json` is suggested for `exif`, `stats`,
and `duplicates`; `focus` supports JPEG sharpness review options.

Completion uses each shell's native facilities. It never runs `photoc`, reads
photo metadata, or requires a CLI parsing/completion package. The Bash script
supports Bash 3.2+, including the bundled macOS Bash. Path completion is not
limited to JPEG extensions, so directories and user-supplied names remain easy
to enter. `--` stops option suggestions.

Run installation commands from the repository root. These examples install
per-user files without administrator privileges; replace an existing photoc
completion file when updating to a newer version.

## zsh

```sh
mkdir -p "$HOME/.local/share/zsh/site-functions"
install -m 644 completions/zsh/_photoc "$HOME/.local/share/zsh/site-functions/_photoc"
```

Add the following to `~/.zshrc` **before** completion initialization:

```zsh
fpath=("$HOME/.local/share/zsh/site-functions" $fpath)
autoload -Uz compinit
compinit
```

If a framework already calls `compinit`, add only the `fpath` line before the
framework is loaded. Open a new shell. In an existing shell, after adding the
directory to `fpath`, run `autoload -Uz _photoc; compdef _photoc photoc` to
register it directly. That assumes `compinit` has already run.

## bash

```sh
mkdir -p "$HOME/.local/share/photoc"
install -m 644 completions/bash/photoc "$HOME/.local/share/photoc/photoc.bash"
```

Add this line to `~/.bashrc` and run it once in the current shell:

```bash
source "$HOME/.local/share/photoc/photoc.bash"
```

For login shells, including common macOS terminal setups, ensure
`~/.bash_profile` sources `~/.bashrc`, or place the same completion source line
in `~/.bash_profile`. The `bash-completion` package is not required.

## fish

```fish
mkdir -p "$HOME/.config/fish/completions"
install -m 644 completions/fish/photoc.fish "$HOME/.config/fish/completions/photoc.fish"
```

If `XDG_CONFIG_HOME` is set, use `$XDG_CONFIG_HOME/fish/completions` instead.
Fish loads the file automatically in new shells. To load it immediately:

```fish
source "$HOME/.config/fish/completions/photoc.fish"
```

Adjust that path too when using a custom configuration directory.

## Try it

Type these prefixes and press Tab:

```text
photoc com
photoc --ver
photoc stats --rec
photoc sort ./photos --by
photoc compress photo.jpg --quality
photoc scrub photo.jpg --in-
```

Use `photoc` on `PATH` for command-name registration. Flags take separate
values (`--quality 80`), not `--quality=80`; do not combine short flags.
Completion does not execute commands or authorize file changes. Preview
rename/sort plans before choosing `--apply`; `scrub --in-place` still replaces
originals without a backup.

## Development checks

Keep the option lists aligned with `src/core/parse.c` and command help.
CTest registers completion tests for each shell available when CMake is
configured; missing shells are reported and do not become build dependencies.
The zsh test exercises the real completion system in a temporary pseudo-terminal.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build -R completion --output-on-failure
```

Native interfaces: [zsh completion system](https://zsh.sourceforge.io/Doc/Release/Completion-System.html),
[Bash programmable completion](https://www.gnu.org/software/bash/manual/html_node/Programmable-Completion.html),
and [fish completion](https://fishshell.com/docs/current/cmds/complete.html).

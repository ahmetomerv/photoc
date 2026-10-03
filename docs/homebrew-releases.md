# Updating the Homebrew tap after a photoc release

This is the manual fallback checklist for the separate
[`ahmetomerv/homebrew-photoc`](https://github.com/ahmetomerv/homebrew-photoc)
tap. Its [`Formula/photoc.rb`](https://github.com/ahmetomerv/homebrew-photoc/blob/main/Formula/photoc.rb)
builds photoc from a tagged source archive. It is separate from the release
installer and is not part of `homebrew/core`.

For the normal release-triggered PR flow and one-time GitHub setup, see
[Automatic Homebrew tap pull requests](homebrew-automation.md).

**Order matters:** publish and verify the new photoc release first, then update
the tap. A new GitHub release does not change the formula by itself. Until the
tap's updated formula reaches `main`, `brew upgrade photoc` cannot install that
version. Homebrew updates tap definitions with `brew update` and installs a
newer formula version with `brew upgrade`.

## 1. Finish the photoc release

Follow [Releasing photoc](releasing.md) from a clean photoc checkout. In short:

1. Bump `VERSION` and update `docs/release-notes.md` and any dependency notices.
2. Build and run CTest, commit and push to `main`, then confirm CI passed for
   that exact commit.
3. Create and push the matching annotated `vX.Y.Z` tag.
4. Wait for the Release workflow to publish the GitHub release. Confirm it is
   neither a draft nor a prerelease, that its builds succeeded, and that the
   expected downloads and checksums are present. Test the release installer as
   described in the release guide.

The examples below start in the photoc checkout after those checks. Run them
in the same terminal session so `$version` and `$tag` remain set. Use the
actual version for that release, not the example `0.5.1` shown later.

```sh
version=$(python3 scripts/package-release.py version)
tag="v$version"
printf 'Updating the tap for %s\n' "$tag"
gh release view "$tag" --repo ahmetomerv/photoc \
  --json tagName,isDraft,isPrerelease \
  --jq '{tagName,isDraft,isPrerelease}'
```

The `gh` check requires GitHub CLI access. You can check the same release on
the [GitHub releases page](https://github.com/ahmetomerv/photoc/releases).
Stop if the release is absent, still a draft, or the wrong version. Do not move
a published tag or replace its assets to repair a release; prepare a new
version instead.

## 2. Calculate the source archive checksum

The formula's `url` points to GitHub's **source archive for the tag**. It does
not use `photoc-<version>-darwin-*.tar.gz`, the raw executables, or the release's
`SHA256SUMS`: those are for the shell installer and prebuilt downloads. Compute
the SHA-256 of the exact source archive that the formula will fetch:

```sh
archive_dir=$(mktemp -d)
curl -fL --retry 3 \
  "https://github.com/ahmetomerv/photoc/archive/refs/tags/$tag.tar.gz" \
  -o "$archive_dir/photoc-$tag.tar.gz"
shasum -a 256 "$archive_dir/photoc-$tag.tar.gz"
```

Copy the 64-character digest from the `shasum` output. If the download fails,
do not edit the formula using an old or guessed digest. Keep the downloaded
archive until Homebrew has fetched and verified the updated formula; then you
can remove this temporary directory.

## 3. Update the tap formula

The local tap created by `brew tap-new` is a Git checkout. Before changing it,
check that it has no work you need to preserve and bring `main` up to date:

```sh
cd "$(brew --repository ahmetomerv/photoc)"
git status --short
git switch main
git pull --ff-only origin main
git switch -c "photoc-$version"
```

If `git status --short` shows changes, review them before switching or pulling.
If the branch name already exists, choose another name. Edit
`Formula/photoc.rb` and change the version in `url` plus its `sha256`. The
current formula infers the version from its URL; it has no separate `version`
field. For a hypothetical `0.5.1` release, these lines would become:

```ruby
url "https://github.com/ahmetomerv/photoc/archive/refs/tags/v0.5.1.tar.gz"
sha256 "THE_NEW_SOURCE_ARCHIVE_SHA256"
```

Replace the placeholder with the real digest. Keep the rest of the formula as
it is unless the release changes build requirements. In that case, update
`depends_on`, the build/install commands, completions, or the formula test as
needed. In particular, check CMake's pkg-config dependencies and installed
file paths when those change. Do not add a `bottle` block just because the
tap's pull-request checks produced bottle artifacts; publishing bottles is a
separate process.

Check the tap's open PRs before making a duplicate. If the release-triggered
workflow already proposed this version, review its URL, source checksum, and
checks, then use that PR instead of creating another branch. The older
`brew bump` workflow can be started manually as another fallback; it is no
longer scheduled.

## 4. Verify the formula locally

Run these from the tap checkout, before pushing. Suppressing Homebrew's
automatic update for these commands keeps it from changing the tap checkout
while you test your local edit:

```sh
HOMEBREW_NO_AUTO_UPDATE=1 brew audit --formula --strict --online \
  ahmetomerv/photoc/photoc
HOMEBREW_NO_AUTO_UPDATE=1 brew fetch --build-from-source \
  ahmetomerv/photoc/photoc
```

`brew fetch` must accept the new URL and checksum. Then build and test the
formula. Choose **one** installation command based on this Mac's state:

```sh
# If photoc is not installed by Homebrew on this Mac:
HOMEBREW_NO_AUTO_UPDATE=1 brew install --build-from-source \
  ahmetomerv/photoc/photoc

# If an older Homebrew photoc is installed on this Mac:
HOMEBREW_NO_AUTO_UPDATE=1 brew upgrade --build-from-source \
  ahmetomerv/photoc/photoc
```

The second command upgrades your own installed copy as part of testing, before
the update is available to other users. Run:

```sh
HOMEBREW_NO_AUTO_UPDATE=1 brew test ahmetomerv/photoc/photoc
brew list --versions photoc
"$(brew --prefix)/bin/photoc" --version
type -a photoc
git diff --check
git diff -- Formula/photoc.rb
```

The Homebrew executable should print the new version. `type -a photoc` catches
an older manual installation earlier in `PATH`; if one exists, do not mistake
its output for the Homebrew build. Fix any audit, fetch, build, test, or
unexpected diff failure before publishing the formula.

## 5. Open and verify the tap pull request

The tap's generated `brew test-bot` workflow checks tap syntax on pushes to
`main`, but runs its formula build checks on **pull requests**. Use a PR for a
release update so the tap gets those macOS and Linux checks before users see
the new version:

```sh
git add Formula/photoc.rb
git commit -m "photoc $version"
git push -u origin "photoc-$version"
```

Open a PR from that branch to `main` in
[`ahmetomerv/homebrew-photoc`](https://github.com/ahmetomerv/homebrew-photoc/pulls).
In its description, link the new photoc release and mention the local audit,
source build, and formula test. Wait for the tap's `brew test-bot` checks and
review the final formula diff. Merge only after the intended source URL and
checksum are correct and the checks pass. The PR may produce bottle artifacts;
the current tap works by building from source, so bottle publication is not
needed for this update process.

If you used an existing `brew bump` PR, perform the same review and test steps
on its proposed formula, then merge that PR rather than pushing a competing
version change.

## 6. Check the published update

After the tap PR is merged, return your local tap to `main`, pull the merge,
and verify what users will receive:

```sh
git switch main
git pull --ff-only origin main
brew update
brew info ahmetomerv/photoc/photoc
brew list --versions photoc
"$(brew --prefix)/bin/photoc" --version
```

If you already installed the new version in step 4, `brew upgrade photoc` has
nothing more to do on this Mac. Otherwise, use the normal user path:

```sh
brew update
brew upgrade photoc
photoc --version
```

A second Mac or a clean Homebrew environment provides the clearest check of a
new user's installation. Users with this tap and formula trust already set do
not need to retap it for each release. Tell users to run `brew update` followed
by `brew upgrade photoc`; `brew update photoc` is not the upgrade command.

## When a release or formula needs a correction

- If the GitHub release failed or contains wrong source, fix photoc and make a
  **new version and tag**. Then update the formula for that new version. Do
  not repoint an existing version to different source bytes.
- If a formula change fails before merging, fix the same tap PR and rerun its
  checks. No photoc source release is needed when the upstream release is sound.
- If users already installed a version and a formula-only change requires them
  to rebuild it, Homebrew's `revision` mechanism can trigger a new installation
  of the same upstream version. Confirm the need, increment the formula's
  revision, and test it through a tap PR. For the next upstream version, remove
  the revision line. See Homebrew's
  [Formula Cookbook](https://docs.brew.sh/Formula-Cookbook#formulae-revisions).

Keep [Homebrew's tap guide](https://docs.brew.sh/How-to-Create-and-Maintain-a-Tap)
and [formula guide](https://docs.brew.sh/Formula-Cookbook) handy when its
commands or policies change.

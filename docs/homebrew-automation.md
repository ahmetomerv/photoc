# Automatic Homebrew tap pull requests

`photoc` is distributed through the separate
[`ahmetomerv/homebrew-photoc`](https://github.com/ahmetomerv/homebrew-photoc)
tap. The automation prepares a pull request after each stable GitHub release;
**you review and merge the PR**. It does not merge the formula or publish
Homebrew bottles automatically. The existing shell installer and release asset
process still work as before.

## How the two repositories work together

1. Follow [Releasing photoc](releasing.md). Its Release workflow builds, tests,
   verifies assets, and publishes the GitHub release.
2. Only after publication succeeds, the separate `Request Homebrew tap PR` job
   checks that the release is published and stable. It sends a
   `repository_dispatch` event containing the tag to the tap.
3. The tap's `Propose photoc release` workflow rechecks the release, downloads
   the **tagged source archive**, confirms its `VERSION`, calculates SHA-256,
   and uses Homebrew's `brew bump-formula-pr` to open a PR for
   `Formula/photoc.rb`. It skips a version already present in the tap or an
   existing PR for that version.
4. The tap's `brew test-bot` builds and tests the formula on its PR runners.
   You review the proposed URL, checksum, dependencies, and checks, then merge.
   Users can then run `brew update` and `brew upgrade photoc`.

The tap's older daily `brew bump` workflow is now manual-only, to avoid two
scheduled paths racing to propose the same version. The
[manual update checklist](homebrew-releases.md) remains a fallback.

## Release-to-tap checklist

Use this checklist for each **new stable version**. The source release and tap
formula are published separately; completing the GitHub release does not make
that version available through Homebrew until the tap PR is merged.

| Stage | Repository | Action or expected result |
| --- | --- | --- |
| Prepare | `ahmetomerv/photoc` | Bump `VERSION`, update release notes, build and test, then push the release commit. |
| Tag and publish | `ahmetomerv/photoc` | Wait for CI on that exact commit, push its annotated `vX.Y.Z` tag, and verify the published GitHub release and assets. |
| Notify | `ahmetomerv/photoc` | The post-publication `Request Homebrew tap PR` job sends the tag to the tap. |
| Propose | `ahmetomerv/homebrew-photoc` | `Propose photoc release` validates the tagged source and opens a formula PR. |
| Review and publish | `ahmetomerv/homebrew-photoc` | Review the formula diff and macOS/Linux checks, then merge the PR. |
| Upgrade | User's machine | Run `brew update`, `brew upgrade photoc`, and `photoc --version`. |

The exact version bump, validation, tag, asset, and installer commands live in
the [photoc release guide](releasing.md). The automated proposal changes only
the formula's tagged source `url` and `sha256`; review any dependency, CMake,
install-path, completion, or formula-test changes yourself before merging.

## One-time configuration (completed)

The tap's `Propose photoc release` workflow is on its default `main` branch,
which is required for `repository_dispatch` to trigger it. The source
repository's `main` branch contains the post-publication notification job and
the manual retry workflow.

The source repository has an Actions secret named
`PHOTOC_TAP_DISPATCH_TOKEN`. Its fine-grained token targets only
`ahmetomerv/homebrew-photoc` with **Contents: Read and write** permission, as
required by the dispatch API. The tap uses its own `GITHUB_TOKEN` to open the
formula PR. In the tap's **Settings → Actions → General → Workflow
permissions**, **Allow GitHub Actions to create and approve pull requests** is
enabled. The tap workflow requests `contents: write` and
`pull-requests: write` for its PR job.

If the dispatch token is revoked or otherwise stops working, create a new
fine-grained token with the same repository access and permission, then update
the `PHOTOC_TAP_DISPATCH_TOKEN` secret in the source repository. Do not put
the token value in Git, an issue, or a PR.

GitHub documents the
[fine-grained token setup](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/managing-your-personal-access-tokens),
[dispatch permission](https://docs.github.com/en/rest/repos/repos#create-a-repository-dispatch-event),
and [Actions PR setting](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/enabling-features-for-your-repository/managing-github-actions-settings-for-a-repository).

## Initial verification without a new release

The maintainer ran **Actions → Retry Homebrew tap notification → Run
workflow** in `photoc` with the already-published `v0.5.0` tag. The source
notification succeeded. The tap's **Propose photoc release** run succeeded
and reported that `0.5.0` was already present, so it opened no PR. This
verified the secret, cross-repository dispatch, and no-update path. The first
future release will exercise PR creation and its build checks.

## What to do for each future release

1. Follow the [photoc release procedure](releasing.md) from a clean source
   checkout. Bump `VERSION`, update `docs/release-notes.md`, build and test,
   commit and push, and wait for CI on the release commit. Push the matching
   annotated `vX.Y.Z` tag. Wait for the
   [Release workflow](https://github.com/ahmetomerv/photoc/actions/workflows/release.yml)
   to publish a stable GitHub release, then verify its notes, assets,
   checksums, and installer as described in the release guide.
2. In that Release run, confirm **Request Homebrew tap PR** succeeded. It
   starts only after publication and uses `PHOTOC_TAP_DISPATCH_TOKEN` from the
   source repository to send the tag to the tap.
3. On the [tap Actions page](https://github.com/ahmetomerv/homebrew-photoc/actions),
   confirm **Propose photoc release** succeeded. Find the new PR on the
   [tap pull requests page](https://github.com/ahmetomerv/homebrew-photoc/pulls).
   If the formula already has that version or a PR for it already exists, the
   workflow reports that and skips a duplicate proposal.
4. Read the PR's `Formula/photoc.rb` diff. Confirm the `url` names the new tag
   and the `sha256` belongs to that tagged **source archive**, not a binary
   release asset. If the release changed dependencies, CMake options, installed
   files, completions, or the formula test, update the PR accordingly.
5. A PR created with the tap's `GITHUB_TOKEN` may show **Approve workflows to
   run**. If so, approve the run, then wait for the macOS and Linux
   `brew test-bot` checks. Review the final diff and merge the PR only after
   those checks pass. A waiting check is not a passing check.
6. Verify the merged formula on the tap's `main` branch. On a machine with an
   older Homebrew installation of photoc, run:

   ```sh
   brew update
   brew upgrade photoc
   photoc --version
   ```

   If that machine already installed the new version while testing the PR,
   `brew upgrade photoc` has nothing further to install. New users can run
   `brew install ahmetomerv/photoc/photoc`.

The automated PR changes only the source `url` and `sha256`. It cannot decide
whether a changed upstream build needs a new Homebrew dependency or install
step. Its PR and tests give you a place to catch that before merging.

## Recovery and manual fallback

- **Notification fails after publication:** The GitHub release remains
  published. Check the `Request Homebrew tap PR` job. If the token is missing,
  expired, or incorrectly scoped, repair the secret and run **Actions → Retry
  Homebrew tap notification** with the published tag. No new photoc release is
  needed.
- **Tap workflow fails:** Read its Actions log. After fixing the workflow or
  tap PR settings, run **Actions → Propose photoc release → Run workflow** in
  the tap with the same tag. It verifies the published release again.
- **No PR appears:** Check for an existing open photoc version PR. If none
  exists, use the [manual Homebrew update checklist](homebrew-releases.md).
- **PR checks wait for approval:** Approve the workflow runs in GitHub, then
  wait for their results before merging. GitHub documents this behaviour for
  PRs created with `GITHUB_TOKEN` in its
  [workflow-trigger guide](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow).
- **The release itself needs correction:** Make a new photoc version and tag
  through the normal release process. Do not move a published tag to change
  the source associated with an existing formula checksum.

The PR creation relies on Homebrew's
[`brew bump-formula-pr`](https://docs.brew.sh/Manpage#bump-formula-pr-options-formula)
command. Recheck the Homebrew and GitHub documentation if their workflows or
permission rules change.

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

## One-time GitHub setup

Deploy the new tap workflow to the tap's **default `main` branch first**.
GitHub handles `repository_dispatch` only when the receiving workflow exists
on that branch. Then deploy the photoc workflow changes. Review the changes in
each repository through a pull request before merging. The files involved are:

- Tap: `.github/workflows/photoc-release.yml`,
  `.github/workflows/autobump.yml`, and `README.md`.
- photoc: `.github/workflows/release.yml`,
  `.github/workflows/notify-homebrew-tap.yml`,
  `scripts/notify-homebrew-tap.py`, its test, and this documentation.

To publish the prepared tap changes, start in its local checkout. Review
`git status --short` first; these commands stage only the tap files listed:

```sh
cd "$(brew --repository ahmetomerv/photoc)"
git status --short
git switch -c codex/photoc-release-dispatch
git add .github/workflows/photoc-release.yml .github/workflows/autobump.yml README.md
git commit -m "Propose photoc tap updates after releases"
git push -u origin HEAD
```

Open a pull request into `main` in `ahmetomerv/homebrew-photoc`, review its
checks, and merge it. If the branch name already exists, choose another name.
Then publish the prepared photoc changes from the source checkout:

```sh
cd /Users/ahmetomerv/Documents/Projects/photoc
git status --short
git switch -c codex/photoc-homebrew-notification
git add .github/workflows/release.yml .github/workflows/notify-homebrew-tap.yml \
  CMakeLists.txt scripts/notify-homebrew-tap.py tests/test_homebrew_notification.py \
  docs/homebrew-automation.md docs/homebrew-releases.md docs/releasing.md docs/README.md
git commit -m "Request Homebrew tap PRs after releases"
git push -u origin HEAD
```

Open a pull request into `main` in `ahmetomerv/photoc`, review its CI, and
merge it. These commands leave unrelated files unstaged; inspect both diffs
before committing. No version tag is needed for this setup change. Future
tagged releases will contain the notification job.

Create one **fine-grained personal access token** in GitHub:

1. Open your GitHub profile **Settings → Developer settings → Personal access
   tokens → Fine-grained tokens → Generate new token**.
2. Set the resource owner to `ahmetomerv`. Select **Only select repositories**
   and choose **`homebrew-photoc` only**.
3. Under **Repository permissions**, give **Contents: Read and write**.
   GitHub's dispatch API requires this permission on the destination tap.
   Leave unrelated permissions unset; Metadata read access is automatic.
4. Choose an expiration you will maintain and generate the token. Copy it
   once. Do not put it in Git, an issue, a PR, or a shell command that could be
   saved in history.
5. In **`ahmetomerv/photoc`** (the source repository), open
   **Settings → Secrets and variables → Actions → New repository secret**.
   Name the secret exactly `PHOTOC_TAP_DISPATCH_TOKEN` and paste the token.
   The tap does not need a copy of this secret; its own `GITHUB_TOKEN` creates
   the PR.

In **`ahmetomerv/homebrew-photoc`**, open **Settings → Actions → General →
Workflow permissions** and enable **Allow GitHub Actions to create and approve
pull requests**. The tap workflow requests `contents: write` and
`pull-requests: write` for its PR job. Keep the token's expiration date in your
maintenance calendar: an expired token leaves the photoc release published but
causes the notification job to fail.

GitHub documents the
[fine-grained token setup](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/managing-your-personal-access-tokens),
[dispatch permission](https://docs.github.com/en/rest/repos/repos#create-a-repository-dispatch-event),
and [Actions PR setting](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/enabling-features-for-your-repository/managing-github-actions-settings-for-a-repository).

## Verify the setup without making a new release

After both repositories contain the new workflows on `main` and the secret and
tap setting are configured:

1. In `ahmetomerv/photoc`, open **Actions → Retry Homebrew tap notification →
   Run workflow**. Enter an already-published stable tag, such as `v0.5.0`.
   This tests the secret and cross-repository dispatch.
2. In `ahmetomerv/homebrew-photoc`, open **Actions → Propose photoc release**.
   Confirm the dispatch run completed. Because the formula already contains
   that version, it should report that no update is needed and open no PR.

This smoke test verifies dispatch and the no-op path. The first future release
will exercise PR creation and its build checks.

## What to do for each future release

1. Complete the ordinary [photoc release procedure](releasing.md). Wait until
   the GitHub release is publicly published and the release workflow's
   **Request Homebrew tap PR** job succeeds.
2. Open the tap's Actions page and confirm **Propose photoc release** succeeded
   and opened a PR. A PR created using the tap's `GITHUB_TOKEN` may show
   **Approve workflows to run**. If so, approve the run so `brew test-bot`
   executes; do not treat waiting checks as passed.
3. Read the formula diff. Confirm that it changes to the expected tag and
   tagged source SHA-256. If the release changed dependencies, CMake options,
   installed files, or completions, update the PR's formula accordingly.
4. Wait for the tap's macOS and Linux `brew test-bot` checks, then merge the PR.
   The formula stays at the old release for users until this merge.
5. Verify the merged formula and, on a machine still at the old Homebrew
   version, run `brew update`, `brew upgrade photoc`, and `photoc --version`.

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

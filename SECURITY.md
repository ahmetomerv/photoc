# Security policy

photoc handles local files and untrusted image data. Please report suspected
security or data-safety defects privately, including:

- File corruption, loss of image data, or changes to an original that should
  have remained untouched.
- Unsafe overwrites, writes outside the intended destination, or bypasses of
  collision checks, including behavior involving symlinks or concurrent changes.
- Malformed JPEG/EXIF input causing memory errors, unexpected file access,
  crashes, or excessive resource use.

If you are unsure whether a defect is security-sensitive, use the private route.
Ordinary bugs and feature requests can use the public issue templates.

## Report privately

Use **Report a vulnerability** on the repository's
[Security Advisories page](https://github.com/ahmetomerv/photoc/security/advisories)
when available. GitHub's [private reporting guide](https://docs.github.com/en/code-security/how-tos/report-and-fix-vulnerabilities/report-privately)
explains the process.

If that button is unavailable, open a public issue titled **Security contact
request** asking for a private reporting channel. Include no vulnerability
details, reproducer, image, or sensitive information in that request. Wait for
a private channel before sharing those details. Do not open a public issue or
pull request containing an undisclosed vulnerability or exploit fixture.

## What to include

- `photoc --version`, the commit if using a development build, OS/architecture,
  and installation method. Include libexif/TurboJPEG versions if known.
- The affected command and exact arguments, including `--apply`, `--in-place`,
  recursion, and output-path options when relevant.
- Expected and actual behavior, exit status, and relevant stdout/stderr or
  sanitizer output. Explain which files were changed and whether a destination
  already existed.
- Minimal reproduction steps using disposable files. Prefer a small synthetic
  or anonymized image fixture and describe any required filesystem conditions.

Logs, paths, and EXIF may contain personal details, GPS coordinates, or camera
serial numbers. Remove sensitive data where possible; share an original photo
only through the agreed private channel when necessary and with permission.

If corruption or an unintended overwrite has occurred, stop modifying affected
files and preserve backups and evidence. Reproduce only with disposable copies.

## Maintenance and disclosure

Security fixes target the latest release and the development branch; older
versions may need an upgrade. Reports about any version are welcome. This is a
small project, so response times may vary; follow up in the private channel.

Maintainers should enable GitHub private vulnerability reporting, triage reports
privately, and coordinate a fix and disclosure with the reporter. Add regression
tests using minimal fixtures and publish affected versions and upgrade guidance
when the fix is available.

<!-- For undisclosed vulnerabilities, follow SECURITY.md before opening a public PR. -->

## Summary

What changed and why? Link the related issue, if any.

## Verification

List commands run and their results. Explain any checks not run.
For behavior changes, include tests; bug fixes should include a regression test.

```text
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## File safety (if applicable)

Describe dry-run/apply behavior, overwrite protection, and failure handling.
Test file changes with disposable fixtures.

## Documentation

Note help/docs updates, new dependencies, or interface changes when relevant.
See [CONTRIBUTING.md](https://github.com/ahmetomerv/photoc/blob/main/CONTRIBUTING.md)
for formatting and development checks.

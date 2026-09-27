# Contributing to photoc

Thanks for helping build `photoc`.

1. Keep changes focused and avoid adding dependencies without a clear need.
2. Use C17 and follow the existing source layout: routing in `src/core/`, command implementations in `src/commands/`, and tests in `tests/`.
3. Build and run the tests before submitting a change:

   ```sh
   sh scripts/build-and-test.sh
   ```

4. Add or update tests when adding behavior. Keep fixtures small and place them in `tests/fixtures/`.
5. Describe what changed and how it was tested in your pull request.

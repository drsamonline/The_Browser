# Aetheris development workflow

Aetheris is an experimental browser project derived from the Ladybird/SerenityOS ecosystem. Keep upstream provenance intact and avoid representing Aetheris-specific additions as upstream work.

## Local validation

```bash
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build --output-on-failure
```

Use a clean working tree before applying a generated batch. After successful validation, inspect the diff before staging:

```bash
git diff --check
git add -A
git diff --cached --check
git status
```

Do not commit generated build directories, local caches, credentials, or unrelated experiments.

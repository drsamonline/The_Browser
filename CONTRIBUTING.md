# Contributing to Aetheris Browser

Aetheris is an experimental derivative browser project. Contributions must respect both Aetheris-specific development work and the provenance of upstream-derived code.

## Before changing code

1. Identify the subsystem being changed.
2. Determine, where reasonably possible, whether the code is upstream-derived or Aetheris-specific.
3. Preserve existing copyright, license, and attribution notices.
4. Avoid unnecessary rewrites of imported infrastructure.
5. Add regression coverage for new behavior where practical.

## Validation

Run the relevant build and tests before committing. Reconfigure CMake when build configuration or registered source files change.

```text
Inspect → Implement → Build → Test → Review diff → Commit
```

## Commit messages

Prefer concise subsystem-oriented messages, for example:

```text
Aetheris Layout: add flex wrapping foundation
Aetheris Rendering: improve text decoration painting
Aetheris Resources: add document resource integration
Docs: clarify upstream provenance
Tests: add resource integration coverage
```

Avoid claiming performance improvements unless they were measured and documented.

## Attribution and licensing

Do not remove or replace upstream notices simply to rebrand code as Aetheris. If provenance or licensing is unclear, document the uncertainty and resolve it before making a broad ownership or relicensing claim.

See [`docs/UPSTREAM_ATTRIBUTION.md`](docs/UPSTREAM_ATTRIBUTION.md).

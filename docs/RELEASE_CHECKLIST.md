# Aetheris release checklist

**Author:** Dr. Sohil Momin · Part of the [Aetheris documentation set](USER_GUIDE.md)

## Source and repository

- [ ] Working tree reviewed.
- [ ] No generated build output or local credentials are staged.
- [ ] Upstream attribution and license notices are preserved.
- [ ] README statements match demonstrated project capabilities.
- [ ] Changelog/release notes describe actual changes only.

## Validation

- [ ] `cmake -S . -B build`
- [ ] `cmake --build build --config Debug`
- [ ] `ctest --test-dir build --output-on-failure`
- [ ] Release smoke test passes as part of CTest.
- [ ] CI status is reviewed for the release commit.

## Release evidence

- [ ] Commit identifier recorded.
- [ ] Platform and compiler recorded.
- [ ] Generator and build configuration recorded.
- [ ] Known limitations recorded.
- [ ] Performance statements are backed by reproducible measurements or omitted.

## Tagging

Create a version tag only after the release candidate has passed the checks above and the repository documentation has been reviewed for provenance and accuracy.

---

*Aetheris Browser — release checklist. Authored and maintained by **Dr. Sohil Momin**.*

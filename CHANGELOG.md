# Changelog

**Author:** Dr. Sohil Momin · Part of the [Aetheris documentation set](docs/USER_GUIDE.md)

All notable Aetheris-specific changes should be documented here. Upstream-derived changes should retain their original provenance and must not be presented as newly authored Aetheris work.

## Unreleased — v0.1 candidate

### Added

- Browser application, tab, input, navigation, and browser-chrome foundations.
- Resource loading, HTTP transport abstraction, cookie-state foundation, and dynamic runtime infrastructure.
- Security-policy and secure-resource-loading foundations.
- Browser chrome state and basic navigation feedback.
- Regression coverage for application, runtime, security, and browser-chrome behavior.
- Final v0.1 acceptance criteria, release checklist, and release smoke coverage.
- Documentation set completed in GitHub style: `docs/USER_GUIDE.md`, `docs/ARCHITECTURE.md`, root `AUTHORS` file, author watermark (Dr. Sohil Momin) across project documents, `.mailmap` identity mapping, and Aetheris CODEOWNERS entries.

### Fixed

- Browser chrome navigation now constructs `NavigationRequest` using the current rendering/navigation API.
- Browser chrome navigation uses the active session viewport width when available.

### Changed

- Project documentation uses explicit Ladybird/SerenityOS-derived provenance and avoids unsupported fixed performance claims.

### Known limitations

Aetheris remains experimental and does not claim complete web-platform compliance, a complete production browser sandbox, or production-ready browser networking.

---

*Aetheris Browser — changelog. Authored and maintained by **Dr. Sohil Momin**.*

# Changelog

**Author:** Dr. Sohil Momin · Part of the [Aetheris documentation set](docs/USER_GUIDE.md)

All notable Aetheris-specific changes should be documented here. Upstream-derived changes should retain their original provenance and must not be presented as newly authored Aetheris work.

## Unreleased — v0.1 candidate

### Added

- GitHub community health files: pull request template (`.github/PULL_REQUEST_TEMPLATE.md`) and Aetheris feature-request issue form (`.github/ISSUE_TEMPLATE/feature_request.yml`).
- Browser application, tab, input, navigation, and browser-chrome foundations.
- Resource loading, HTTP transport abstraction, cookie-state foundation, and dynamic runtime infrastructure.
- Security-policy and secure-resource-loading foundations.
- Browser chrome state and basic navigation feedback.
- Regression coverage for application, runtime, security, and browser-chrome behavior.
- Final v0.1 acceptance criteria, release checklist, and release smoke coverage.
- Documentation set completed in GitHub style: `docs/USER_GUIDE.md`, `docs/ARCHITECTURE.md`, root `AUTHORS` file, author watermark (Dr. Sohil Momin) across project documents, `.mailmap` identity mapping, and Aetheris CODEOWNERS entries.
- `Standalone` and `StandaloneSize` CMake presets, plus the `AETHERIS_SIZE_OPTIMIZED` profile (`-Os`, dead-section elimination, LTO, stripped) for minimum-footprint builds of the Aetheris targets.
- A "known gaps and open work" section in `ISSUES.md`.
- A minimal HTTP/1.1 client (`HttpClient`, `include/aetheris/network/http_client.hpp`) with DNS resolution via `getaddrinfo`, a bounded (non-blocking) connect timeout, and Content-Length, chunked and read-until-close body handling.
- Headless `aetheris --fetch <url>` and `aetheris --render <url> [out.ppm]` modes; the executable now links `AetherisRendering` and can fetch a URL and render it to a PPM end to end.
- `TestAetherisHttpClient`: a loopback-server regression test covering DNS + HTTP round-tripping.
- A dependency-free 8x8 bitmap font (`bitmap_font.{hpp,cpp}`, public-domain font8x8 data) with real glyph rasterisation: `DrawText` now paints actual glyphs instead of a solid cell, and text measurement and rasterisation share one advance so glyphs line up with layout.
- `TestAetherisBitmapFont`: regression coverage for glyph lookup, metrics and rasterisation.
- A dependency-free PNG decoder (`png.{hpp,cpp}`, colour types 0/2/3/4/6, 8/16-bit, non-interlaced) with its own DEFLATE inflater; the image cache now decodes PNG as well as PPM, and relative `src`/`href` references are resolved against the document URL so fetched pages' images load. `TestAetherisPng` covers RGB/RGBA/grey/palette inputs.

### Fixed

- Browser chrome navigation now constructs `NavigationRequest` using the current rendering/navigation API.
- Browser chrome navigation uses the active session viewport width when available.
- HTML tokenizer no longer truncates tag names at the first non-letter character, so `h1`–`h6` and custom elements (e.g. `my-element`) keep their full name.
- CSS selector matching now supports descendant (` `) and child (`>`) combinators; previously only the right-most compound was matched, so rules such as `#id .class` or `section > p` never applied. Specificity is computed as CSS (a,b,c) without double-counting class or element identifiers.
- Inherited properties (notably `color` and `text-decoration`) now reach descendants: initial values are applied after inheritance instead of before, so text nodes render with their ancestor's colour.
- Absolutely positioned elements resolve percentage widths against the containing block rather than against the already-resolved width.
- `overflow: hidden`/`clip` now clips descendants only; an element's own background and border are no longer clipped away by its own overflow.
- Offscreen surfaces (`SoftwareSurface`, `RenderDocument::render_to_surface`) default to a transparent clear colour instead of opaque white.

### Performance

- The CSS cascade takes a fast path for selectors without combinators and no longer copies the selector string per (node, selector) pair.
- The paint tree skips the per-node child sort when no child sets a `z-index`.
- The release optimization profile is now portable: LTO and dead-strip flags are chosen per toolchain and probed before use (previously `-flto=auto -ffat-lto-objects` and `-Wl,--gc-sections` were applied to every Clang/GNU build, which broke non-GCC toolchains and Apple's linker). `-Ofast` was dropped because it enables fast-math and contradicts the project's `-ffp-contract=off` determinism.

### Changed

- The HTML tree builder no longer synthesises implicit `<html>`/`<body>` wrappers around fragment input; parsed trees match the document as authored, consistent with the `AetherisRendering` regression suite.
- Removed committed build output (`build-standalone/`, `build-docs/`) from the repository and taught `.gitignore` to ignore build trees.
- Removed dead code: unused `StyleResolver::is_inherited_property`, `DomNode::last_child`, `VisualInteractionState::mark_paint_dirty`, `FormRuntime::clear_focus`, `CookieJar::clear_origin`, `Viewport::to_viewport_coordinates` and `DocumentRuntime::set_text`, and merged split namespace blocks in `software_surface.cpp`, `navigation.cpp` and `browser_session.cpp`.
- Rewrote `ISSUES.md` (previously Ladybird boilerplate with stale project references) and refreshed `README.md`, `docs/ARCHITECTURE.md` and the landing page for the new build profiles and the footprint story.
- Project documentation uses explicit Ladybird/SerenityOS-derived provenance and avoids unsupported fixed performance claims.

### Known limitations

Aetheris remains experimental and does not claim complete web-platform compliance, a complete production browser sandbox, or production-ready browser networking. Only `http://` URLs are reachable: TLS is not implemented, and DNS resolution is synchronous. Text rendering uses a single embedded 8x8 bitmap font: there is no vector font, font selection, kerning or shaping.

---

*Aetheris Browser — changelog. Authored and maintained by **Dr. Sohil Momin**.*

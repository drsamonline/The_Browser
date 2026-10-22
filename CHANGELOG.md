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
- CSS `border` (width/style/colour, any order) and `background` (colour + `url()` image) shorthands, with `TestAetherisCssShorthands`.
- A baseline JPEG decoder (`jpeg.{hpp,cpp}`, SOF0, 8-bit, 1/3 components, restart intervals); the image cache decodes JPEG as well as PNG/PPM. `TestAetherisJpeg` covers 4:4:4 colour and grayscale.
- A GIF decoder (`gif.{hpp,cpp}`: LZW, global/local colour tables, interlacing, transparency); the image cache decodes GIF too. `TestAetherisGif` covers a palette fixture.
- Anti-aliased text: `SoftwareSurface::draw_text` now supersamples each glyph cell (4x4 coverage) and blends by coverage, giving grayscale edges instead of hard pixels.
- Optional HTTPS/TLS (`tls.{hpp,cpp}`, `AETHERIS_HAS_TLS`): a real OpenSSL handshake with certificate and hostname verification against the system trust store (or `AETHERIS_CA_FILE`); `HttpClient` now serves `https://`. `TestAetherisTls` performs a hermetic handshake against an in-process OpenSSL server (trusted connect + self-signed rejection).
- Interlaced (Adam7) PNG support in the decoder; `TestAetherisPng` now checks an interlaced image decodes identically to the same non-interlaced image.
- Progressive JPEG (SOF2) support: multiple scans with spectral selection and successive approximation, sharing one coefficient buffer and reconstructing after the last scan. `TestAetherisJpeg` decodes a progressive fixture.
- A minimal SVG rasteriser (`svg.{hpp,cpp}`): rect, circle, ellipse, line, polyline, polygon and path (Béziers flattened), fill + stroke, viewBox and translate/scale/matrix transforms, rendered with 2x supersampling; the image cache renders SVG. `TestAetherisSvg` covers shapes, paths and paints.
- WebP decoding via the system libwebp (lossy VP8 and lossless VP8L), wired into the image cache and compiled in only when libwebp is found (`AETHERIS_HAS_WEBP`). `TestAetherisWebp` decodes lossless (exact) and lossy (approximate) fixtures.
- A self-contained mini JavaScript engine (`src/js/`, the `AetherisJS` library): lexer, recursive-descent parser and tree-walking interpreter covering variables, operators, control flow, functions/recursion, arrays, objects and built-ins (`console.log`, `Math`, `parseInt`/`parseFloat`, string and array methods, `document.write`). `aetheris --eval <script>` runs a snippet and `--render` executes inline `<script>` blocks, injecting `document.write` output before layout. `TestAetherisJs` covers the core. (The full LibJS engine in the tree is a bytecode VM needing Rust + a clang GC plugin + ICU and is not built in the standalone profile.)
- Expanded the mini JavaScript engine with String methods (`split`, `replace`, `substring`, `repeat`, `trim`, `includes`, `startsWith`, `endsWith`), Array methods (`forEach`, `map`, `filter`, `reduce`, `reverse`, `sort`, `concat`), numeric indexing on strings and arrays, `Object.keys` and `Array.isArray`, and a `CREDITS.md` documenting upstream attribution.
- Added closures, the `new` operator with constructor functions and `this` binding for methods, `JSON.stringify`/`JSON.parse`, and `Math.trunc`/`Math.sign` to the mini JavaScript engine.
- Added a prototype chain to the mini JavaScript engine: functions get a `prototype` object, `new` links instances to it, property lookup walks the chain, `Object.prototype` is extensible, and the `instanceof` operator is supported.
- Minimal DOM bindings: `document.getElementById(id)` returns an element handle whose `textContent` can be read and written, and `--render` parses the document, runs inline scripts, then applies `textContent` changes (and `document.write` output) before layout. `RenderDocument::create_from_document` builds from an already-parsed document so a host can mutate it first.
- Added `class` syntax to the mini JavaScript engine: `constructor` and methods are desugared onto a prototype object, so `new`/`instanceof`/inheritance work with classes as well as constructor functions.
- Added the `for...of` loop (over arrays and strings) and the `switch`/`case`/`default` statement to the mini JavaScript engine.
- Extended the DOM bindings: `document.createElement`, `element.appendChild`, `element.setAttribute` and `element.getAttribute`, so scripts can build and insert new elements before layout.
- Added events to the mini JavaScript engine: `window`/`document` `addEventListener` and `on<type>` handlers, dispatched by the host (`DOMContentLoaded`, `load`). Fixed a lifetime bug where function bodies aliased a parsed program that was freed when `run()` returned, so handlers invoked after the script (e.g. at load) could dangle; parsed programs are now retained.
- Added a compact backtracking regular-expression engine (`src/js/regex.{hpp,cpp}`) with literals, character classes, anchors, alternation, groups and the *, +, ?, {n,m} quantifiers and the `g`/`i` flags; wired to regex literals, `RegExp`, `test`/`exec`, and `String.match`/`search`/`replace`/`split`. Capture groups, back-references and look-around are not supported. Fixed `new` precedence so `new X(a).y()` parses as `(new X(a)).y()`.
- Added async support to the mini JavaScript engine: a microtask queue, `Promise` (`then`/`catch`/`finally`, `resolve`/`reject`/`all`), `async` functions and `await`. There is no event loop, so `await` drains the microtask queue synchronously; the queue is also drained after each script and after events.
- Added `Map` (get/set/has/delete/keys/values/size), `Set` (add/has/delete/size), `Date` (`Date.now`, `new Date(ms)`, getters, `toISOString`/`toString`, UTC) and `Array.prototype.includes` to the mini JavaScript engine.
- Added element-level events: element handles expose `addEventListener` and `on<type>` handlers, and `--render ... --dispatch <id> <type>` fires an event on a named element after load (with `this` bound to the element).

### Fixed

- `parse_http_url` no longer overrides an explicit port on `https://` URLs (it previously forced 443).

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

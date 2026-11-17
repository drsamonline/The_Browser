# Aetheris roadmap and status

**Author:** Dr. Sohil Momin · Part of the [Aetheris documentation set](USER_GUIDE.md)

This is the authoritative status document: what is done, how it is evidenced, and
what remains. Anything not listed as done is not done, regardless of how it looks.

Last verified: 57/57 CTest passing, size profile **2,630,800 bytes (~2.51 MB)** (v0.4).
The current state has been audited — findings and the plan forward are in
[AUDIT.md](AUDIT.md).

## Before any item on this list

**Read `docs/UPSTREAM-FIRST.md`.** This engine is derived from Ladybird, so
`Libraries/LibWeb` usually already solves what is being built — focus, hover,
`:active`, event bubbling and style invalidation all live there. When upstream has
no answer, say why in `DESIGN.md` rather than inventing quietly.

## Done

### Engine foundations

- **C++20 engine**, standalone build with no dependency on the upstream tree.
  Targets: `AetherisRendering`, `AetherisRuntime`, `AetherisJS`, `aetheris`.
- **HTML parsing: gumbo** (vendored, Apache-2.0). The HTML5 tree-construction
  algorithm: implied end tags, the spec's stray-`</p>` rule, automatic
  `<tbody>`, character references, error recovery. `Document::parse_html`
  converts gumbo's tree into the internal `DomNode` tree, splicing out the
  implicit `<html>`/`<head>`/`<body>` wrappers when the document did not author
  them.
- **CSS**: parser, cascade, selector matching (tag/`.class`/`#id`/compound/
  descendant), inline `style` attributes and inline `<style>` blocks.
- **Layout**: block and inline flow, box model, line boxes.
- **Paint**: ordered paint commands with clipping onto a software surface.
- **Text**: anti-aliased bitmap font.
- **Images**: PNG (own DEFLATE inflater), JPEG, GIF, SVG, WebP (via libwebp).
- **Networking**: HTTP/1.1 client with redirects, a cookie jar honouring
  `Domain`/`Path`/`Expires`/`Max-Age`/`Secure`/`HttpOnly`, a
  `Cache-Control: max-age` response cache, and per-origin keep-alive reuse that
  probes a pooled connection before sending into it.
- **TLS**: HTTPS via OpenSSL, optional at build time (`AETHERIS_HAS_TLS`).
- **Upstream data tables**: the full HTML5 character-reference table (2,125
  entries) and the 148 CSS named colours, generated from upstream Ladybird /
  SerenityOS data.

### JavaScript

- **QuickJS** (vendored, MIT) is the engine — ES2025, not a subset. Arrow
  functions, template literals, destructuring, spread/rest, optional chaining,
  `Symbol`/`WeakMap`, real `Date`/`Map`/`Set`, correct number formatting.
- **DOM bindings**: `document.write`, `getElementById`, `createElement`,
  `appendChild`, `insertBefore`, `remove`, `setAttribute`/`getAttribute`,
  `textContent` (accessor), `innerHTML` (get/set), `style` (string set plus
  `setProperty`/`removeProperty`), `classList`, `tagName`, `dataset`,
  `parentNode`/`children` (live getters), `querySelector`/`querySelectorAll`,
  `document.body`/`head`/`documentElement`/`title`, `window.innerWidth`/
  `innerHeight`, and `getComputedStyle(el).getPropertyValue(name)` resolved
  through the cascade.
- **Events**: `addEventListener` (with a capture option) and `on<type>` on
  window, document and elements, dispatched through the full three phases —
  capture (root to target), target, bubble (target to root) — with event
  objects (`type`, `target`, `currentTarget`, `defaultPrevented`) and
  `preventDefault`/`stopPropagation`/`stopImmediatePropagation`.
  `DOMContentLoaded` and `load`.
- **Promise jobs** are drained after each script and each event.
- **Timers**: `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval` and
  `queueMicrotask`, run on **virtual time** — timers fire in due order without
  real waiting, which is what makes them usable in a headless renderer.

### Tooling and process

- **CTest suite: 51 tests**, including `AetherisDomHost`, which drives the real
  binary end to end and fails on allocator-corruption markers in stderr.
- **Size is measured and reported**, never enforced: the size profile prints the
  stripped figure and its drift from the recorded reference
  (`Tools/report_binary_size.cmake`).
- **CI**: standalone Linux and Windows jobs are the supported signal. The
  full-tree Windows build is opt-in only. Tagged releases are published from the
  standalone size profile.
- **Provenance**: `CREDITS.md` records every upstream source, including the
  vendored QuickJS and gumbo and the generated data tables.

### Releases

- **v0.1** — tagged, evidenced in [V0.1_ACCEPTANCE.md](V0.1_ACCEPTANCE.md).
- **v0.2** — tagged, evidenced.

## Remaining

Ordered by value. **The execution order — cheap wins first, batched by shared
code path, large tasks last — is in [PHASES.md](PHASES.md).**

Two rules earned on 2026-11-17, after a review of this list found one false
claim, one understated blocker and one stale line:

- **Batch by shared code path, not by feature.** Custom properties, sibling
  combinators, `[a=v i]` and the form-state pseudo-classes all touch the CSS
  parser and the resolver: one build, one test cycle, four features. The font
  API, text layout and paint are one file chain and must not be split across
  sessions.
- **Every item carries the step that proves it.** A capability is not started
  until there is a way to see it working through the real pipeline — a reftest
  shape, a pixel, or a counter in the window summary.

### High value

1. **Vector fonts and text shaping.** The renderer measures text a byte at a
   time — `FontMetrics::advance(char)` in `src/rendering/font.hpp` — so a
   rasteriser cannot be wired in until the font API asks for glyphs. **That API
   change is the first step, not the last.** **Font parsing is done**
   (`src/rendering/ttf_font.*`: `cmap` formats 4/12,
   `head`/`hhea`/`maxp`/`hmtx`/`loca`/`glyf`, simple-glyph outlines in font
   units). **A glyph rasteriser is done** (`src/rendering/glyph_rasterizer.*`:
   quadratic flattening, scanline fill with vertical supersampling and exact
   horizontal coverage, producing anti-aliased coverage bitmaps).
   Order: (a) font API returning glyph ids and per-glyph advances, and
   `TextLayout::measure_text` using them; (b) paint through the rasteriser;
   (c) kerning; (d) shaping, Arabic and Indic last. (a) is the blocker for
   everything else in this row.
2. ~~`requestAnimationFrame`~~ — **done** (`performance.now()` and
   `requestAnimationFrame` on virtual time). Still missing: a compositor and a
   real clock, so a page expecting wall-clock pacing runs as fast as the loop
   allows.
3. ~~`getComputedStyle`~~ — **done** (read-only resolved values).

3a. ~~Scrolling~~ — **done**: paint-time offset, wheel and key routing,
   scrollbar geometry, `--render-window --scroll-y` for headless verification.
4. **CSS coverage**: complete `flex`/`grid`, pseudo-elements
   (`::before`/`::after` need layout-level synthetic boxes — upstream generates
   the pseudo-element's computed style beside the element's, `DOM/Element.cpp`),
   `@font-face`, `position: sticky`, transitions/animations.
   **CSS custom properties (`var()`) are done as of 2026-11-17** — inherited by
   prefix, collected before substitution, resolved at style time with a depth
   limit for cycles.
   **Media queries, the structural pseudo-classes (`:first-child`,
   `:last-child`, `:only-child`, `:nth-child(n)`, `:empty`, `:root`,
   `-of-type`, `An+B`), `:is()`/`:where()`/`:has()`/`:not(list)` and `transform`
   (translate/scale) are done.**
   `position: sticky` was deliberately deferred while there was no scroll
   offset to stick to. **The scroll offset now exists** (`PaintOffset` in the
   paint pass, `Viewport` in the window loop, wheel/PageUp/PageDown/Home/End
   routed by `InputRouter`), so `position: sticky` is no longer blocked by
   missing infrastructure — it is now ordinary layout work.
5. ~~Form elements~~ — **done**: `value` on `input`/`textarea`/`select`/`option`,
   `checked`, and `form.submit()` firing a cancelable `submit` event.

### Medium value

6. ~~Event model completeness~~ — **done** (capture, `stopPropagation`,
   `preventDefault`, event objects).
7. **Networking**: HTTP/2 only. Certificate validation (chain, hostname,
   expiry) was already enforced in `src/network/tls.cpp`; it is now **proven** by
   `Tools/check_tls_verification.sh`, which checks that an untrusted certificate
   is refused, one trusted via `AETHERIS_CA_FILE` is accepted, and a trusted
   certificate for the wrong host is still refused.
8. **Security model**: mixed-content blocking, same-origin policy enforcement in
   the script bindings, and the rest of CSP. **CSP is partly done**: the
   `<meta http-equiv="Content-Security-Policy">` policy is read and inline
   scripts are gated by it (`src/main.cpp`). What is missing is the fetch
   directives — `img-src`, `connect-src`, `style-src` and the rest — so the work
   is finishing an existing path, not building one. Process/site isolation is
   **out of scope** (see below).
9. **A real UI shell**: the *window layer exists* — `src/ui/sdl_window.cpp`,
   `window_host.cpp`, `window_manager.cpp`, `browser_session.cpp`,
   `input_router.cpp`, with a chrome bar and a tab count in the window summary.
   What is missing is the shell **UI**: no tab strip, no bookmarks, no history
   view. The binary in this repository's default build runs the headless backend
   and writes PPM frames, which is why the summary line reports a headless
   backend rather than a window.

### Out of scope

- **Process and site isolation.** Upstream Ladybird does not ship it in this
  shape either; it is a multi-year architectural project, not a browser-engine
  task. Keeping it on this list made every honest estimate of "when is it safe"
  wrong. The security bar this engine aims at is stated in
  `docs/HANDOVER.md`: TLS verification, CSP for inline scripts, and
  same-origin enforcement in the bindings.

### Lower value / deferred

10. **`requestAnimationFrame` and compositor-driven rendering.**
11. **Accessibility tree.**
12. **Developer tools / inspector.**
13. **Full-tree build (LibJS/LibWeb).** Needs Rust, ICU and a clang GC plugin.
    A dependency decision, not a task. The opt-in Windows full-tree CI job exists
    for this and is not expected to pass yet.

## Known limitations to state honestly

- The engine is **not** standards-complete. It renders simple and moderately
  complex pages; it does not render the modern web correctly.
- Timers run on **virtual time**, not a real clock, and there is no
  `requestAnimationFrame`.
- The browser is **headless**: `--render` writes a PPM image.
- Size is ~2.38 MB, roughly 40× under a WebKitGTK browser and 100× under
  Firefox, while still running JavaScript. Dillo (~0.25 MB) is far smaller but
  has no JavaScript.

## Definition of done

A capability is done when: it is implemented, the CTest suite passes, an
end-to-end check exercises it through the real pipeline (not only a unit test),
and the docs and changelog state the capability and its limitations.

---

*Aetheris Browser — roadmap. Authored and maintained by **Dr. Sohil Momin**.*

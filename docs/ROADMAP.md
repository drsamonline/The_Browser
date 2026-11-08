# Aetheris roadmap and status

**Author:** Dr. Sohil Momin · Part of the [Aetheris documentation set](USER_GUIDE.md)

This is the authoritative status document: what is done, how it is evidenced, and
what remains. Anything not listed as done is not done, regardless of how it looks.

Last verified: 51/51 CTest passing, size profile **2,490,416 bytes (~2.38 MB)**.

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
- **Networking**: HTTP/1.1 client with redirects, a host-scoped cookie jar, a
  `Cache-Control: max-age` response cache, and per-origin keep-alive reuse.
- **TLS**: HTTPS via OpenSSL, optional at build time (`AETHERIS_HAS_TLS`).
- **Upstream data tables**: the full HTML5 character-reference table (2,125
  entries) and the 148 CSS named colours, generated from upstream Ladybird /
  SerenityOS data.

### JavaScript

- **QuickJS** (vendored, MIT) is the engine — ES2025, not a subset. Arrow
  functions, template literals, destructuring, spread/rest, optional chaining,
  `Symbol`/`WeakMap`, real `Date`/`Map`/`Set`, correct number formatting.
- **DOM bindings**: `document.write`, `getElementById`, `createElement`,
  `appendChild`, `setAttribute`/`getAttribute`, `textContent` (accessor),
  `innerHTML` (get/set), `style` (string set plus `setProperty`/
  `removeProperty`), `classList`, `parentNode`/`children`,
  `querySelector`/`querySelectorAll`.
- **Events**: `addEventListener` and `on<type>` on window, document and
  elements, with bubbling up the ancestor chain. `DOMContentLoaded` and `load`.
- **Promise jobs** are drained after each script and each event.

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

Ordered by value, not by effort.

### High value

1. **Vector fonts and text shaping.** The renderer uses a bitmap font, so text
   quality is the single largest visual gap against any real browser. Needs a
   TrueType/OpenType rasteriser, kerning and basic shaping.
2. **An event loop.** Promise jobs are drained after each script and event;
   there are no timers, no `requestAnimationFrame`, and no continuous job
   processing. Async code that resolves later than the current turn does not run.
3. **`getComputedStyle`.** Resolved styles are not exposed to scripts.
4. **CSS coverage**: `transform`, complete `flex`/`grid`, pseudo-elements,
   media queries, `@font-face`, `position: sticky`, transitions/animations.
5. **Form elements**: `input`/`select`/`textarea` values, `submit` events,
   `preventDefault`.

### Medium value

6. **Event model completeness**: capture phase, `stopPropagation`,
   `preventDefault`, event objects with `target`/`currentTarget`.
7. **Networking**: HTTP/2, complete certificate validation, RFC-complete cookies
   (`Domain`/`Expires`/`Secure`/`HttpOnly`), retry when a pooled keep-alive
   connection was closed by the server.
8. **Security model**: process/site isolation, CSP, mixed-content blocking,
   same-origin policy enforcement in the script bindings.
9. **A real UI shell**: the browser is headless and renders to PPM. There is no
   window, no tab strip, no bookmarks or history UI in the shipped binary.

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
- There is **no JavaScript event loop**, so timers and late async work do not
  run.
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

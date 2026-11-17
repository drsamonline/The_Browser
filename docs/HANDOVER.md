# Handover — Aetheris Browser

**For:** any session or agent picking this up
**Repo:** `drsamonline/The_Browser`
**Owner:** Dr Sohil Momin
**Date:** 16 November 2026
**Latest tag:** v0.6.2

Read this before changing anything. It records what works, what does not, and
the mistakes that cost the most time.

---

**Read `docs/UPSTREAM-FIRST.md` first.** This engine is derived from Ladybird, so
`Libraries/LibWeb` already solves focus, hover, `:active`, event bubbling and style
invalidation. Port the shape; do not re-derive it.

---

## 1. What this is

A minimal, from-scratch browser engine in C++20 — HTML5 parsing, CSS cascade and
layout, JavaScript, its own rasteriser, and a compact SDL2 window. It is **not**
a Chromium/Gecko/WebKit wrapper.

| Property | Value |
| --- | --- |
| Licence | MIT |
| Language | C++20 |
| Size profile | ~2.5 MB stripped (report-only, never enforced) |
| Tests | 58 CTest suites, all passing |
| Platforms | Linux (verified), Windows (verified at runtime 16 Nov) |

Vendored: `third_party/gumbo` (Apache-2.0, HTML5 parsing),
`third_party/quickjs` (MIT, JS engine). Embedded DejaVu Sans subset for text.
All credited in `CREDITS.md`.

---

## 2. Current state

### Works

- HTML5 parsing (gumbo) → DOM, with scripts, styles and implied tags
- CSS: cascade, specificity, media queries, structural pseudo-classes,
  `transform` (translate/scale), user-agent stylesheet, length units
  (px/em/rem/%/pt), **shorthand properties** (`margin`, `padding`), `margin: auto`
- JavaScript: QuickJS, events (capture/bubble), timers, `requestAnimationFrame`,
  DOM API (`innerHTML`, `style`, `dataset`, `value`, `checked`, `submit`)
- Text: TrueType parsing, anti-aliased rasteriser, glyph cache, embedded font,
  1,218 kerning pairs
- Networking: keep-alive, cookies, TLS with **bundled CA file**, CSP for inline
  scripts, `SecureResourceLoader` on every fetch
- UI: 34 px chrome strip, tab session, input router, SDL2 window
- Scrolling: wheel, PageUp/PageDown/Space/Home/End, proportional scrollbar,
  clamped viewport, `--render-window --scroll-y` for headless checks
- Releases: Linux tarball + Windows zip with all runtime DLLs

### Known broken / missing

| Item | Detail |
| --- | --- |
| JS after load | Scripts run at load; clicks, keydown and hover dispatch into the DOM and re-lay out; CSS `:hover`, `:active` and `:focus` restyle the element; a click focuses and keys reach the focused element then bubble to document; repeating timers tick once per frame. `keyup`, `mouseleave`, Tab order works |
| Page load | One path: `create_scripted_document`. `--render` passes a negative timer budget to drain the queue; `--window` passes 0 and ticks per frame |
| Window layout | **Correct as of 2026-11-17** — an earlier note in this file claimed the window laid block elements out inline and that the user-agent stylesheet was missing. Both were wrong. The frame shows two divs stacked, 400 px wide, their same-coloured backgrounds merging visually; the sheet probe showed `css=63 combined=1729` on both paths (UA sheet included). Corrected in the audit log, `2026-11-17-aetheris-retraction-window-layout.md`. |
| ~~`display` on an anchor~~ | **Closed 2026-11-17: not reproducible.** Raw pixels for both fixtures in both modes are identical — first link at x 0-400, second at x 0-400 on the next row, nothing at x 600: `plain/render`, `plain/window`, `extra/render`, `extra/window` all `(200,20)=(0,0,255) (600,20)=(255,255,255) (200,150)=(0,0,255) (600,150)=(255,255,255)`. With `--key tab`, frame 1 is blue and frames 2-3 magenta at the first link, everything else unchanged. The earlier side-by-side render came from a build that no longer exists; two attributions were made for it and both were wrong. |
| ~~`:focus-within` with Tab~~ | **Verified 2026-11-17**: an ancestor matches when a child is Tab-focused, asserted by pixel in `AetherisWindowScripts`. |
| `activeElement` during load | Falls back to `body` as upstream does, but only from the first frame after load. Inline scripts that run while the page is being built see `null`, because the window has no page to ask yet. |
| Form controls | Render as bordered boxes now (`input`/`textarea`/`select`/`button`), but they are not interactive: no value editing, no caret, no click-to-type. Their width comes from `min-width: 8em`; an input appears to fill its line, which makes hit-testing ambiguous. |
| ~~Test harness vs CLI~~ | **Resolved 2026-11-17: the harness was right and my assertion was wrong.** The negative case tested "not magenta" as `! (r > 200 && b > 200)`, and white is (255,255,255), so the condition matched white and the assertion failed on a correct pixel. Re-running the harness's exact commands by hand showed frame-3 magenta for a click on the field and white for a click away, as it should. "Magenta" must be tested as magenta: `r > 200 && g < 100 && b > 200`. |
| `:focus-visible` text-entry branch | **Proven, and covered by a test** in `AetherisWindowScripts` (2026-11-17). With `width: 12em` the pair discriminates from the CLI: clicking the field paints it magenta (pointer focus, so only the text-entry clause can match), clicking away leaves it white. The equivalent assertions do not pass inside the test harness, for the reason in the row above. |
| Clicks | **2026-11-17.** A click relayouts only when it mutated the DOM. It used to relayout unconditionally, and `relayout()` with no mutation on record treats the call as structural — cache cleared, page laid out — so clicking a static page was the most expensive thing the window could do. The same mutation-serial gate guards the timer and key paths. |
| Attribute gate | An attribute write relayouts unless the name is in `attribute_is_rendering_relevant` (in `main.cpp`) or a selector mentions it. Adding a code path that reads a new attribute means adding it to that list, or the page will silently stop updating. |
| Attribute selectors | All six forms: `[a]`, `[a=v]`, `[a~=v]`, `[a|=v]`, `[a^=v]`, `[a$=v]`, `[a*=v]`. No case-insensitivity flag (`[a=v i]`), no namespaces. `uses_attribute` scans selector text, so it stays correct for the forms that exist. |
| Style cache | Resolved styles are cached per node, keyed on the parent's `StyleProperties::fingerprint()`. Invalidated by: the mutated node plus ancestors plus descendants (text and attribute writes); for a structural mutation the parent's subtree and the chain above it — the parent alone when the sheet has no positional selector — instead of the whole cache (2026-11-17: appending a span inside one row of 200 went from 407 resolves to 6); and on an interaction-state change only the elements whose pseudo-class state moved (see the row below). The DOM reports which parent's child list changed via `note_child_list_mutation_at()`; several in one frame accumulate. A selector that depends on a *sibling* (other than `:first-child`/`:last-child`/`:nth-child`, which structural mutations already clear for) would break this — none exists yet, and `+`/`~` are not matched. |
| Interaction-state invalidation | **2026-11-17.** Upstream's `ancestor_traversal_for_pseudo_class()` (`CSS/Invalidation/PseudoClassInvalidator.cpp`): `:focus-within` is the only pseudo-class that propagates to ancestors, which matches what `style.cpp` tests. A state move dirties the subtrees and ancestor chains of the element that lost it and the one that gained it. A **focus** move also dirties the ancestor's whole subtree — `.card:focus-within .title` matches a sibling of the focused element — but only when a selector puts `:focus-within` on a non-final compound *that this ancestor matches*. The question is asked of the ancestor itself (`StyleResolver::matches_ancestor_pseudo_class_rule`), as upstream's selector analysis does: `.card:focus-within .title` only affects the card, so a focus move elsewhere stays at 4 resolves where the sheet-wide answer was 408. A compound that matches a wide ancestor (`body:focus-within .title`) still re-resolves the page. |
| Incremental layout | **First increment, 2026-11-17.** `relayout()` compares a per-node layout key against the keys the last pass used. Unchanged means no box moved, so it copies the recorded geometry (`rect`, box model, text fragments) onto the freshly built tree and rebuilds only the render tree. Text and structural mutations never skip. The window summary reports `layouts:` beside `relayouts:`; a relayout that only repainted shows in one and not the other. The key comparison is over the nodes whose style was re-resolved, not the page — `key checks:` reports that count, 5 against 406 nodes on a 200-row page after a hover — because only a node that re-resolved can have a different key. The key is the whole resolved style **minus a deny-list** of the sixteen properties that can only repaint (`kPaintOnlyProperties` in `render_document.cpp`), so a property nobody classified costs a wasted pass instead of a stale frame. It was an allow-list until 2026-11-17 and was missing `gap`, `row-gap`, `column-gap`, `flex-direction`, `flex-grow`, `align-items`, `justify-content`, `grid-template-columns` and `aspect-ratio` — a `gap`-only class toggle shipped a stale frame. Adding a property that paints only means adding it to the deny-list; anything else is covered. Within a pass, a node whose subtree nothing restyled and that sits at the origin and available width it sat at last time takes its recorded geometry and is not entered — `layout nodes:` reports how many were actually laid out, text runs included (5 of 405 for a text edit in row 100 of 200; 405 when the first row grows and everything below moves). The invariant is that a node's subtree geometry is a function of (style, x, y, available_width, its DOM subtree); the parent decides x, y and the width, which is why all three are in the cache key. Images cannot affect layout here (`resolve_images` runs after `layout`), which is why an image swap needs no pass; if that order ever changes, this skip becomes wrong for `src`. Geometry is reused across interaction-state changes too, through the same key comparison. Upstream's equivalent is `RequiredInvalidationAfterStyleChange` (`CSS/UpdateStyle.cpp`), which asks the style *diff* for `needs_relayout()` and relayouts only the marked subtree. The document-wide skip asks the resolved style before and after; the within-pass reuse above is the marked-subtree half, keyed on the origin rather than on an invalidation flag. |
| Frame dumps and `/tmp` | A window frame is 1024x768x3 = 2.3 MB. `AetherisWindowScripts` writes ~58 MB of them and this sandbox's `/tmp` is a 64 MB tmpfs, so the test now removes them on the way out and its own new dump goes under the build tree. A second `ctest` run from an uncleaned `/tmp` fails elsewhere for a disk reason: `AetherisDomHost` reported "render exits 0" when the disk was full and passed on a clean `/tmp`. Check `df -h /tmp` before believing a failure. |
| Shaping | No Arabic/Indic/ligatures; kerning only |
| `position: sticky` | No longer blocked: a scroll offset now exists, so this is ordinary layout work |
| Scrollbar drag | A click on the track scrolls to that point; dragging the thumb is not implemented |
| `file://` in `--render-window` | No backend for the scheme on that path ("no backend supports the URL"); use `--render` or serve over loopback |
| `::before`/`::after` | Needs layout-level synthetic boxes |
| HTTP/2 | Deferred |
| Debug overlay newlines | `draw_debug_overlay` ignores `\n` |
| Tab click/close | Router emits actions; not wired to the session |
| `AetherisWindowHost` on Windows | Excluded from the release job — hangs in `SDL_Init` on a headless runner, unexplained |
| ~~`AetherisHttpClient` flaky~~ | **Fixed 16 Nov**: the fixture's accept thread exited on a 10 s idle `select`, so it could disappear between connections under load. It now stops on a flag. 10.04 s → 0.09 s. Still excluded from the release gate; consider re-enabling |

---

## 3. Build

The build needs a compiler. There is **no system compiler** in the sandbox, so
the zig wrappers are used:

```bash
pip install cmake ninja ziglang
# wrappers at /scratch/work/toolchain/zigcc and zigcxx (strip -Werror)
#             /scratch/work/tc_strict/zigcc and zigcxx (keep -Werror)

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=<zigcc> -DCMAKE_CXX_COMPILER=<zigcxx> \
  -DCMAKE_C_COMPILER_WORKS=1 -DCMAKE_CXX_COMPILER_WORKS=1 \
  -DAETHERIS_STANDALONE_BUILD=ON -DAETHERIS_ENABLE_TESTING=ON -DBUILD_TESTING=ON \
  -DOPENSSL_INCLUDE_DIR=/usr/include/node \
  -DOPENSSL_SSL_LIBRARY=/usr/lib/x86_64-linux-gnu/libssl.so.3 \
  -DOPENSSL_CRYPTO_LIBRARY=/usr/lib/x86_64-linux-gnu/libcrypto.so.3 \
  -DAETHERIS_WEBP_LIBRARY=/usr/lib/x86_64-linux-gnu/libwebp.so.7

cmake --build build --parallel
ctest --test-dir build --no-tests=error
```

Size profile: add `-DAETHERIS_SIZE_OPTIMIZED=ON`. The option is **not**
`AETHERIS_SIZE_PROFILE` — getting it wrong measures a different binary.

SDL2 backend: `-DAETHERIS_WITH_SDL2=ON` plus SDL2 installed.

### Verifying a change

- Check the **exit code**, never grep the output for "error". A missing compiler
  and a broken build both produce no matching lines.
- Compile with `-Werror` (the `tc_strict` wrapper). CI uses it; a plain build
  does not.
- **Configure the strict build as Release.** A Debug strict build fails before
  it reaches any of our code: the project defines `_FORTIFY_SOURCE`, glibc warns
  that it needs `-O`, and `-Werror` turns that warning into an error in every
  translation unit. Release is what CI does and it is the only meaningful
  strict configuration.
- Render a page and look at it: `aetheris --render-window <url> out.ppm`.
- Exercise input without a display:
  `--focus-key <x> <y>` focuses with a keyboard trigger, for testing
  `:focus-visible` before Tab exists.
  And otherwise:
  `aetheris --window <url> 2 --click <x> <y> --key j --move <x> <y> --press <x> <y>`
  (`--render` also takes `--click <x>,<y>`; `--window` accepts both spellings).

---

## 4. Releasing

Push a `v*` tag. CI (`.github/workflows/build-aetheris.yml`) builds and publishes:

| Job | Purpose |
| --- | --- |
| `test-standalone` | Linux build + CTest |
| `test-windows-standalone` | Windows build + CTest |
| `test-sdl2-backend` | Builds with SDL2, runs the window loop under `SDL_VIDEODRIVER=dummy` |
| `release-standalone` | Linux tarball + `cacert.pem` |
| `release-windows-standalone` | Windows zip + all DLLs + `cacert.pem` |
| `build-windows` | Full-tree, opt-in only |

**Verify the artifact, not the job colour.** Download the zip and list its
contents. Several releases went green with a broken or missing package.

---

## 5. Gotchas that cost the most time

1. **SDL pixel format.** Our surfaces are `0xRRGGBBAA`; on little-endian that is
   memory `A,B,G,R`. SDL names formats by the 32-bit *word*, so the correct
   constant is `SDL_PIXELFORMAT_RGBA8888`. `ABGR8888` reads the alpha byte as
   red — the chrome rendered bright red for three releases.
2. **`assert()` in tests.** Release defines `NDEBUG`, which compiles assertions
   away: tests passed silently and then failed to build under `-Werror`. Test
   targets now compile with `-UNDEBUG`.
3. **Windows packaging.** The exe links SDL2, OpenSSL and the MSVC runtime
   dynamically. Every one must be in the zip or the user gets `0xC0000135` with
   no explanation. Also: the runner's PATH has MySQL's OpenSSL before the real
   one — search explicit directories first.
4. **`shell: pwsh` exit codes.** GitHub's pwsh shell exits with `$LASTEXITCODE`;
   a `where.exe` miss leaves it at 1, so a successful step reports failure.
5. **Windows TLS.** `SSL_CTX_set_default_verify_paths` finds nothing. Ship
   `cacert.pem` beside the exe; that is the reliable fix.
6. **`relayout()` discards the interaction state.** It rebuilt layout with no
   `VisualInteractionState`, so `:hover` and `:focus` were silently dropped
   whenever anything called it — `dispatch_click_at` does, immediately after a
   click focuses. `RenderDocument` now remembers the last state and passes it to
   `relayout()`. This cost two sessions and was only found by printing the
   hit-test result and the focused pointer, not by reading the code.
7. **Upstream is the reference for anything interaction-shaped.** `LibWeb`'s
   `Document::set_focused_area` (blur -> scoped style invalidation -> set -> focus),
   `Element::is_focused()` asking `document().focused_area()`, and
   `CSS::Invalidation::invalidate_style_after_pseudo_class_state_change` are the
   design this engine should converge on. Reading them ended a three-session loop:
   Aetheris compared state pointers in the style engine and threw the state away in
   `relayout()`, where upstream keeps it on the document and invalidates only what
   depends on it.
8. **The DOM ancestor chain stops at the document element.** `dispatch_bubbling`
   walks `parent` up to the root; the JS `document` and `window` objects are not in
   that chain, so a handler registered on `document` never saw a click or a key
   until each dispatch was followed by `js.dispatch_event(...)`. Upstream bubbles
   through the document and window as part of the same path. Anything that
   dispatches an event needs that second call.
9. **A focus change does not change the DOM.** `need_render` was only set when the
   document fingerprint moved, so moving focus relaid out the page and never
   redrew it: `:focus-visible` "did not work" for two sessions while the state and
   the layout were both correct. Any state change that alters the cascade without
   touching the DOM must set `need_render` itself.
10. **CSS shorthands and units.** `margin: 5em auto` was ignored entirely (only
   longhands were read) and `2em` was parsed as 2 pixels. Both fixed — but this
   class of gap is common: assume a property is unimplemented until proven.
7. **Sandbox recycles.** Uncommitted work is lost. Commit early. Re-clone and
   reinstall the toolchain after a recycle.
8. **A test that is slow is usually a test that is wrong.** `AetherisHttpClient`
   took ten seconds to pass because it waited out an idle timeout, and the same
   timeout is what made it fail under load. When a suite test takes seconds, look
   for a sleep or a timeout before looking for a network problem.
9. **Never use an idle timeout as a shutdown condition in a fixture.** Three
   fixtures did (`AetherisHttpClient` twice, `AetherisDomHost`), and all three
   failed intermittently under `-j 8` because a loaded runner can pause longer
   than the timeout between two connections. Use a stop flag polled by a short
   select, set before `join()`.
10. **A pooled connection can die between the probe and the write.** `HttpClient`
    retries once on a fresh connection now; do not remove that retry, it is the
    difference between a blank page and a working one against servers that close
    idle connections.

---

## Check the instrument before the subject

Three investigations on 2026-11-17 blamed the engine and were wrong three times: a pixel sample read as a
picture, a verdict string a script computed backwards, and an assertion whose colour test also matched
white. The engine was correct throughout.

1. Print the raw value, never a derived verdict (`print(pixel)`, not `print("SIDE-BY-SIDE")`).
2. Test a colour as that colour, never as "not the other one".
3. Look at the frame before reasoning from coordinates.
4. A control case must be able to come out the other way.

## 6. Where things live

| Area | Files |
| --- | --- |
| Entry point, modes, chrome document | `src/main.cpp` |
| HTML → DOM | `src/rendering/document.cpp` |
| CSS parse | `src/rendering/css_parser.cpp` |
| Cascade, matching | `src/rendering/style.cpp` |
| Layout, units, shorthands | `src/rendering/layout.cpp` |
| Paint commands | `src/rendering/render_tree.cpp` |
| Raster surface | `src/rendering/software_surface.cpp` |
| Fonts | `src/rendering/ttf_font.*`, `glyph_rasterizer.*`, `font_library.*` |
| Default styles | `src/rendering/user_agent_stylesheet.cpp` |
| Units | `src/rendering/css_units.hpp` |
| Networking, TLS | `src/network/` |
| UI | `src/ui/` — `window_host`, `sdl_window`, `input_router`, `browser_session` |

Docs: `docs/ROADMAP.md` (status), `docs/PHASES.md` (order), `docs/UI_SPEC.md`
(chrome), `docs/AUDIT.md` (findings), `docs/ARCHITECTURE.md`.

---

## 7. Next work, in priority order

1. **An exact dirty flag instead of a fingerprint.** `document_fingerprint` decides
   whether a timer changed anything by hashing names, text and attributes. It
   misses a change that leaves every string identical (a canvas, a pseudo-element
   style) and it walks the tree once per frame on pages with timers. A revision
   counter incremented by the JS setters would be exact and cheaper.
3. **`keyup` and a focused element.** `keydown` and `mouseover`/`mouseout` dispatch;
   `keyup` does not, and with no focus model every key goes to the document rather
   than to the element the user is typing into.
4. **Scrollbar thumb dragging** — a track click scrolls to that point already;
   a drag needs pointer capture between MouseDown and MouseUp.
3. **`position: sticky`** — now unblocked by the scroll offset.
4. **Wire tab click/close** to the session; address-bar focus styling.
5. **Shaping** for non-Latin scripts.
6. **Debug overlay newlines** — small, visible.
7. **Understand the Windows `SDL_Init` hang** and re-enable that test.
8. **Re-enable `AetherisHttpClient`** in the release gate; the flake is fixed.

---

## 8. Working in a shared environment

This repo lives beside other browser projects worked on by other people.

- **Only touch `drsamonline/The_Browser`.** Do not modify, delete or "clean up"
  other repositories, shared caches or another agent's build directories.
- **Commit and push to `main` in small, self-contained commits.** Do not rewrite
  history, force-push, or move an existing tag.
- **Never delete a release or tag** without explicit instruction.
- **Do not reintroduce a failing size budget.** Size is reported, never enforced.
- **Attribute upstream work.** gumbo, QuickJS and the DejaVu subset are credited
  in `CREDITS.md`; keep it current.
- **Leave the tree building and green.** If you cannot finish a change, revert it
  rather than leaving `main` red.

---

*Aetheris Browser — handover. Maintained by **Dr Sohil Momin**.*

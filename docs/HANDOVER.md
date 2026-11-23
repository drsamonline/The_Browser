# Handover — Aetheris Browser

**For:** any session or agent picking this up
**Repo:** `drsamonline/The_Browser`
**Owner:** Dr Sohil Momin
**Date:** 23 November 2026
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
| Tests | 65 CTest suites, all passing (`ctest -j2`; two cores, never `-j4`) |
| Platforms | Linux (verified), Windows (verified at runtime 16 Nov) |

Vendored: `third_party/gumbo` (Apache-2.0, HTML5 parsing),
`third_party/quickjs` (MIT, JS engine). Embedded DejaVu Sans subset for text.
All credited in `CREDITS.md`.

---

## 1a. What changed on 2026-11-22

- **Row 2d4 closed.** Shaping was not slow: the *hash* was. The face cache keyed on a content hash of
  the font file, and that hash ran on every shaped run — 1.2331 ms of a 1.2571 ms run at -O2 (98%),
  30.6 ms of a 34 ms run at -O0. HarfBuzz itself was 0.0046 ms. The hash now runs once per load
  (`TtfFont::identity()`), and shaped runs are cached the way upstream caches them
  (`Libraries/LibGfx/Font/Font.h`, `ShapingCache`): **0.0001 ms cached, 0.0086 ms cold** at -O2.
- The suite got faster for the same reason: 210 s -> 126 s at `ctest -j2`, because every measured
  string used to pay that hash.
- `tools/shaping-bench.cpp` and `tools/run-shaping-bench.sh` exist so the next claim about shaping cost
  is a measurement, not a memory of one.
- **Row 2e closed** (second batch, same day): bidi level 1. `bidi.cpp` classifies code points, splits
  text at the direction change, and `shape_run()` shapes one buffer per run with the direction forced,
  so Arabic joins and ligates instead of coming out in isolated forms. A line's runs are still painted
  in logical order — row **2e2** carries the rest, and the checklist entry says what it looks like
  from outside (a multi-word RTL line with its words reversed). Suite 65/65.
- **Row 2e2 closed** (third batch): bidi level 2. `visual_order()` gives every fragment on a line a level
  and `TextLayout::layout` places them in that order, so an Arabic line's words land where a reader
  expects them; `direction: rtl` now reaches text through the cascade (`direction` added to the inherited
  list, as `CSS/Properties.json` says). Rows 2e3 (`text-align: start`/`end`) and 2h (layout measures per
  code point, paint draws shaped glyphs) added from what this batch exposed.
- **Row 2h closed** (2026-11-23): one metric for layout and paint. `TextLayout::measure_text` returned the
  sum of code point advances, which skips kerning entirely — "To" measured 19.5625 px against the 16.8594 px
  the pen drew at 16 px — so every fragment's box was wider than its glyphs and centred or wrapped text sat
  off by half the error. It now returns `Font::measure()`, the shaped width, plus spacing.
- **Row 2e3 closed** (2026-11-23): `text-align: start`/`end` resolve against `direction`, and an unset
  value is `start` — so a `direction: rtl` block is right-aligned, as a browser does it. `justify` is still
  treated as `start`.
- **Row 2f closed** (2026-11-23): line breaking by cluster. `safe_break_offsets()` returns the byte offsets
  HarfBuzz does not flag `HB_GLYPH_FLAG_UNSAFE_TO_BREAK`, and a word too long for a line is broken there —
  only when `overflow-wrap: break-word|anywhere` or `word-break: break-all` asks, and never inside a
  ligature, a kerned pair or a letter-plus-mark cluster.
- **Row 2g closed** (2026-11-23): `letter-spacing` and `word-spacing` are applied to the glyph advances in
  the shaper, and the shaped-run cache key carries both. Before this, layout counted the spacing and the
  pen drew without it — letter-spaced text was laid out wider than it painted.
- **Rows 2b and 2c ticked** (2026-11-23): both had been done in code for weeks and never ticked — status
  drift, not new work. `AetherisGlyphRasterizer` now asserts that paint's entry point (`cached_glyph`) returns
  exactly what the rasteriser produces, and `AetherisShaping` asserts kerning in both paths (GPOS and the
  embedded pair table).
- **Row 3a6 closed** (2026-11-23): `content` is a `ContentValue` — a kind (Normal/None/List), a list of items
  and an optional alt-text list — instead of a quoted string. `content: "a" "b"` now renders "ab" rather than
  "a b", `content: "x" / "alt"` keeps the alt text on the layout node, and an item this engine cannot express
  (`attr()`) renders nothing instead of its own source text.

## 2. Current state

### Works

- HTML5 parsing (gumbo) → DOM, with scripts, styles and implied tags
- CSS: cascade, specificity, media queries, structural pseudo-classes,
  `transform` (translate/scale), user-agent stylesheet, length units
  (px/em/rem/%/pt), **shorthand properties** (`margin`, `padding`), `margin: auto`
- Selectors: compound, descendant, child, **sibling `+` and `~`**; attributes including
  **`[a=v i]`**; `:hover`/`:focus`/`:active`/`:focus-visible`/`:focus-within`;
  `:first-child`/`:last-child`/`:only-child`/`:nth-child(An+B)`/`:nth-last-*`/`-of-type`/`:empty`/`:root`;
  **`:is()`/`:where()`/`:not(list)`/`:has()`** including `:has(> .x)`, `:has(+ .x)`, `:has(~ .x)`;
  **`:checked`/`:disabled`/`:enabled`/`:placeholder-shown`**
- **Custom properties and `var()`** — inherited by prefix, fallbacks, chains, cycle guard
- **`visibility`** — inherited, checked per box in paint, `collapse` treated as `hidden`
- JavaScript: QuickJS, events (capture/bubble), timers, `requestAnimationFrame`,
  DOM API (`innerHTML`, `style`, `dataset`, `value`, `checked`, `submit`,
  **`getBoundingClientRect`, `focus()`, `blur()`, `localStorage`, `location`**)
- Navigation: **`location.href`/`assign()`/`reload()`, and a click follows a link** (no `replace()`,
  no `history`, no realm isolation — see the rows below)
- Text: TrueType parsing, anti-aliased rasteriser, glyph cache, embedded font,
  1,218 kerning pairs, **HarfBuzz shaping** (ligatures, Arabic joining, GPOS kerning and mark
  positioning) behind a **shaped-run cache** keyed by (font identity, size, text) — a cached run is
  0.0001 ms at -O2, measured by `tools/run-shaping-bench.sh`
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
| `el.style` | **2026-11-17.** The declared `CSSStyleDeclaration` surface: `cssText`, `getPropertyValue`, `setProperty`, `removeProperty` (returns the removed value), `length`, `item(i)`. **`el.style.background = v` does not work and is not claimed** — upstream's `CSS/CSSStyleDeclaration.idl` has no named-property accessor, so there is nothing to port; a page sets a property with `setProperty` or `cssText`. |
| Attribute writes | **2026-11-17.** `rendering::set_attribute()` / `remove_attribute()` (in `dom_mutation.hpp`) are the only way to write an attribute on a live document: they write and note, in the order `js_set_attribute` established (node-scoped first, because it clears the attribute name). Six runtime writers bypassed this and were silent no-ops — `classList`, `className`, `el.style = cssText`, `el.style.setProperty`, `el.dataset.x`, `el.checked`, `el.value`. Upstream gets the same guarantee from the type system (`Element::m_attributes` is private; every write runs `run_attribute_change_steps`). A new DOM API that writes an attribute must use the choke point. |
| `classList` / `className` | **Fixed 2026-11-17.** `classList.add/remove/toggle` wrote the class attribute with no mutation note, so it changed the DOM and never restyled — a silent no-op in the renderer. `className` was not implemented at all. Both go through `set_class_attribute()`, which writes the attribute and notes it in the order `js_set_attribute` uses. Any new DOM API that writes an attribute must note it the same way. |
| Inline wrap opportunities | **2026-11-18.** An inline item breaks only where whitespace precedes it — `line_ends_in_whitespace` or the item's own leading whitespace — which is upstream's `text_item_wrap_opportunity()`. Two things must be true together: the flag has to be set in the **text** branch (it `continue`s before the non-text path), and an inline child must be **measured against the whole line** before the fit test, because shrink-to-fit otherwise clamps it to the space left. Debug aid: `AETHERIS_TRACE_INLINE=1` prints each child's kind, data, cursor, width and the fit decision. |
| Inline layout | **2026-11-18.** Inline-level children flow left to right on one line, and an inline box with no specified width is as wide as its content (`layout_node`'s shrink-to-fit), which is upstream's rule from `Rust/src/layout/inline_formatting_context.rs`. **The trap is two layers deep**: a text run's box must be its fragments' extent, not the remaining line width — while it was the latter, every ancestor's shrink-to-fit saw a full-width child and nothing worked, including two attempted fixes. Wrapping at the container edge is not measured yet (row 3g). |
| Text measurement | **2026-11-18.** Code points, not bytes: `utf8_next()` in `font.hpp` decodes one code point (U+FFFD for malformed, always advancing), `FontMetrics::advance_for_code_point()` asks the TTF for `advance_width(glyph_index(cp))`, and `TextLayout::measure_text` and `draw_text`'s outline path iterate with it. Letter-spacing is added once per **code point**; upstream adds it per shaped cluster, which is the same thing until shaping exists. The bitmap fallback stays byte-wise: it is ASCII art with no other cells. |
| `var()` | **2026-11-18.** Resolved at style time by `substitute_vars()`: `--*` entries are ordinary properties, inherited by prefix in `inherit_from()`/`inherit_custom_properties()`, collected in a pass **before** substitution, and substituted in the cascade and in the inline `style` attribute. Three orderings matter and each was wrong first. Cycles stop at depth 8 and leave the declaration unsubstituted, so it is dropped rather than taking a wrong value. No `env()`/`attr()`, no `@property`, no typed custom properties. |
| Sibling selectors and invalidation | **2026-11-18.** `+` and `~` match right-to-left over previous element siblings. `CssStyleSheet::uses_sibling_sequence()` is the single question the invalidation asks — a positional pseudo-class **or** a sibling combinator at any depth — and it drives two things: a child-list change widens to the parent's subtree, and a state change or attribute change dirties the changed node's siblings. Skip either and a rule matches correctly at load then goes stale on mutation. |
| `:has()` | **2026-11-18.** Descendant, `>` and the sibling forms. Invalidation needed no new machinery beyond the sibling rows above, because the dirty sets already include ancestors. |
| `getBoundingClientRect()` | **2026-11-18.** Returns the layout node's rect — the border box in document coordinates — as a plain object with `x`/`y`/`width`/`height`/`left`/`top`/`right`/`bottom`. **No forced layout read**: a script that runs at load gets a zeroed rect, where upstream forces the read and answers with real numbers. Read inside a timer, which is where pages use it. |
| `focus()` / `blur()` | **2026-11-18.** A script call records a **queue** entry and the loop performs it with `set_focus()`, the same helper a click or Tab uses. A queue, not a slot: `run_timers()` runs every timer due in one frame, so `focus(); blur();` inside one callback overwrote a single slot and neither happened. Focusability is upstream's three-level rule — `tabindex` parses (base), `a` with `href`, `button`/`select`/`textarea`/`input` when enabled, `input` not `type=hidden` — plus the rendering check: `display:none` refused, `visibility:hidden` refused, and **no layout node yet allowed** so `focus()` at load is not refused. The click path uses the same rule and walks up to the nearest focusable ancestor, moving nothing if there is none. |
| `sessionStorage` | **2026-11-18.** The same six members over a **second** map on the interpreter, per origin. Upstream keeps a session bottle map per origin and tab and hands out the same `Storage` interface over it (`HTML/Window.cpp`, `session_storage`). Ours ends with the process, which is what a session store does when the tab closes. The test's discriminating assertion is that each store cannot see the other's key — two maps that were accidentally one would pass everything else. |
| `localStorage` | **2026-11-18.** Six members: `length`, `key(i)`, `getItem`, `setItem`, `removeItem`, `clear`. The store is a map on the interpreter, which lives as long as the window, so it survives a navigation — proved end to end with two documents. **In memory only** (upstream persists per profile) and **`key(i)` is sorted, not insertion order** (the store is a `std::map`). A missing key is `null`, not `undefined`. |
| URL resolution | **2026-11-18.** Two implementations, deliberately: `resolve_url()` in `main.cpp` for links and scripts, and `Url::resolve()` in `src/rendering/url.cpp` for resources. **`Url::resolve` had an infinite loop** on a `..` at the root — `pos - 1` underflowed when the `..` was the first segment, `rfind` searched from the end, and the replace was a no-op, so `/../up` never shrank. Fixed: the root case drops the segment, every branch must make progress, and a branch that does not stops. `TestUrl` covers it. |
| Navigation | **2026-11-18.** `location.href` (get/set), `assign()`, `reload()`, and a click on `<a href>` navigates unless a handler called `preventDefault()`. The JS module owns `location` and asks the embedder through `set_navigation_hooks`, so it knows nothing about sessions or fetching. URL resolution is absolute, root-relative or last-segment-relative — **not RFC 3986**: no dot-segment removal, no query merging. **No realm isolation**: the interpreter survives the navigation, which is what makes `localStorage` persist and also why the old page's globals and listeners survive. No `replace()`, no `history`, no `location.pathname`/`search`/`hash`. |
| Clicks | **2026-11-17.** A click relayouts only when it mutated the DOM. It used to relayout unconditionally, and `relayout()` with no mutation on record treats the call as structural — cache cleared, page laid out — so clicking a static page was the most expensive thing the window could do. The same mutation-serial gate guards the timer and key paths. |
| Attribute gate | An attribute write relayouts unless the name is in `attribute_is_rendering_relevant` (in `main.cpp`) or a selector mentions it. Adding a code path that reads a new attribute means adding it to that list, or the page will silently stop updating. |
| Structural pseudo-classes | `:first-child`, `:last-child`, `:only-child`, `:nth-child(n)`, `:nth-last-child(n)`, `:first-of-type`, `:last-of-type`, `:only-of-type`, `:nth-of-type(n)`, `:nth-last-of-type(n)`, `:empty`, `:root`, and `:not(<compound>)`. **`:empty`, `:root`, the -of-type family and `:not()` were added 2026-11-17.** `:empty` counts child nodes and their character data: comments and doctypes do not, and neither does zero-length text. `:not()` takes one compound, no combinators, and recurses through the matcher — so `:not(:first-child)` works. **`An+b` notation** (`:nth-child(2n+1)`, `odd`, `even`, `-n+3`) works as of 2026-11-17, using upstream's predicate from `Rust/src/css/style/selector.rs`. `:is(<list>)`, `:where(<list>)` and `:not(<list>)` work as of 2026-11-17, with upstream's specificity (`:is`/`:not` take the greatest argument, `:where` takes zero). **Two splitters must stay paren-aware**: the rule parser splits selectors on commas and the combinator splitter splits on whitespace — both used to break inside `:is(...)`. `:has(<relative selector list>)` works as of 2026-11-17 — `:has(.x)` matches any descendant, `:has(> .x)` a direct child; the sibling forms answer false because `+`/`~` are not matched. **A functional pseudo-class must consume to its matching `)` even when the capture stopped at a marker inside it** — `:has(> .x)` was dropped for two commits because the fix-up only fired when the value ended with `(`. Not implemented: the `of S` argument. |
| Positional-selector list | `CssStyleSheet::uses_positional_pseudo_class()` is the one list the structural invalidation consults. Adding a pseudo-class to `style.cpp` and forgetting this list is how a page goes stale — they are one edit apart on purpose. |
| Custom properties | **2026-11-17.** `--*` entries are ordinary properties: inherited by prefix in `inherit_from()`/`inherit_custom_properties()`, collected in a pass before substitution, resolved by `substitute_vars()` at style time with a depth limit of 8 for cycles. `var()` is resolved in the cascade and in the inline `style` attribute. Three orderings matter and each was wrong first: inherit by prefix, collect before substitute, inherit **before** the cascade. |
| Attribute selectors | All six forms: `[a]`, `[a=v]`, `[a~=v]`, `[a|=v]`, `[a^=v]`, `[a$=v]`, `[a*=v]`. No case-insensitivity flag (`[a=v i]`), no namespaces. `uses_attribute` scans selector text, so it stays correct for the forms that exist. |
| Style cache | Resolved styles are cached per node, keyed on the parent's `StyleProperties::fingerprint()`. Invalidated by: the mutated node plus ancestors plus descendants (text and attribute writes); for a structural mutation the parent's subtree and the chain above it — the parent alone when the sheet has no positional selector — instead of the whole cache (2026-11-17: appending a span inside one row of 200 went from 407 resolves to 6); and on an interaction-state change only the elements whose pseudo-class state moved (see the row below). The DOM reports which parent's child list changed via `note_child_list_mutation_at()`; several in one frame accumulate. A selector that depends on a *sibling* (other than `:first-child`/`:last-child`/`:nth-child`, which structural mutations already clear for) would break this — none exists yet, and `+`/`~` are not matched. |
| Interaction-state invalidation | **2026-11-17.** Upstream's `ancestor_traversal_for_pseudo_class()` (`CSS/Invalidation/PseudoClassInvalidator.cpp`): `:focus-within` is the only pseudo-class that propagates to ancestors, which matches what `style.cpp` tests. A state move dirties the subtrees and ancestor chains of the element that lost it and the one that gained it. A **focus** move also dirties the ancestor's whole subtree — `.card:focus-within .title` matches a sibling of the focused element — but only when a selector puts `:focus-within` on a non-final compound *that this ancestor matches*. The question is asked of the ancestor itself (`StyleResolver::matches_ancestor_pseudo_class_rule`), as upstream's selector analysis does: `.card:focus-within .title` only affects the card, so a focus move elsewhere stays at 4 resolves where the sheet-wide answer was 408. A compound that matches a wide ancestor (`body:focus-within .title`) still re-resolves the page. |
| Incremental layout | **First increment, 2026-11-17.** `relayout()` compares a per-node layout key against the keys the last pass used. Unchanged means no box moved, so it copies the recorded geometry (`rect`, box model, text fragments) onto the freshly built tree and rebuilds only the render tree. Text and structural mutations never skip. The window summary reports `layouts:` beside `relayouts:`; a relayout that only repainted shows in one and not the other. The key comparison is over the nodes whose style was re-resolved, not the page — `key checks:` reports that count, 5 against 406 nodes on a 200-row page after a hover — because only a node that re-resolved can have a different key. The key is the whole resolved style **minus a deny-list** of the sixteen properties that can only repaint (`kPaintOnlyProperties` in `render_document.cpp`), so a property nobody classified costs a wasted pass instead of a stale frame. It was an allow-list until 2026-11-17 and was missing `gap`, `row-gap`, `column-gap`, `flex-direction`, `flex-grow`, `align-items`, `justify-content`, `grid-template-columns` and `aspect-ratio` — a `gap`-only class toggle shipped a stale frame. Adding a property that paints only means adding it to the deny-list; anything else is covered. Within a pass, a node whose subtree nothing restyled and that sits at the origin and available width it sat at last time takes its recorded geometry and is not entered — `layout nodes:` reports how many were actually laid out, text runs included (5 of 405 for a text edit in row 100 of 200; 405 when the first row grows and everything below moves). The invariant is that a node's subtree geometry is a function of (style, x, y, available_width, its DOM subtree); the parent decides x, y and the width, which is why all three are in the cache key. Images cannot affect layout here (`resolve_images` runs after `layout`), which is why an image swap needs no pass; if that order ever changes, this skip becomes wrong for `src`. Geometry is reused across interaction-state changes too, through the same key comparison. Upstream's equivalent is `RequiredInvalidationAfterStyleChange` (`CSS/UpdateStyle.cpp`), which asks the style *diff* for `needs_relayout()` and relayouts only the marked subtree. The document-wide skip asks the resolved style before and after; the within-pass reuse above is the marked-subtree half, keyed on the origin rather than on an invalidation flag. |
| `innerHTML` and parent links | **Fixed 2026-11-17.** `js_inner_html_set()` inserted parsed children with `push_back()`, so they kept a parent pointer to the fragment root that died with the block. Nothing walked `parent` after an `innerHTML` set until the layout dirty-set walk did, and that was a SIGSEGV. Every insertion path must go through `DomNode::append_child()`. Found by the reftest matrix, not by reasoning. |
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

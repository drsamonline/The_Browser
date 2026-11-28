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
- **Row 3i closed as a non-divergence** (2026-11-23): the row said the engine never overflows. Measured, it
  overflows whenever there is no break opportunity — a word wider than the line stays on one line, and two
  inline boxes with no whitespace between them stay on one line too — and wraps at a space. Five assertions in
  `AetherisTextLayout` now pin it so a future wrap change cannot quietly start wrapping.
- **Row 3b closed** (2026-11-23): `position: sticky` is a paint-time shift of the box's command range, clamped
  to its containing block, so scrolling stays repaint-only. Sticky children sort above in-flow siblings because
  `position: sticky` creates a stacking context — without that the shifted box was painted underneath the next
  sibling.
- **Row 3a3 closed** (2026-11-23): a generated box paints its background and its text. The box commands had been
  in the `else` of the text branch, so the one node kind that carries both painted one of them; they now paint
  before the text. Found alongside: a generated `inline-block` took the whole line instead of shrinking to its
  text, because no parent shrink-to-fit pass can measure a node with no DOM node.
- **Row 3c1 closed** (2026-11-23): flex completeness. The `flex` shorthand is parsed (`flex: 1` = `1 1 0%`, which is
  what real pages mean by it), `flex-basis` feeds the base size, and an overflowing row shrinks items in proportion
  to `flex-shrink x base`. Grid areas are row 3c2, split out because they are a different algorithm.
- **Row 3c2 closed** (2026-11-23): grid placement. `grid-template-areas` is parsed into a name grid, `grid-area`
  puts an item in that name's bounding box, `grid-column`/`grid-row` take a 1-based line or `start / end`, and
  auto-placed items take the first free cell. The parser had to keep the quotes on `grid-template-areas` first —
  the same fault `content` had in 3a6.
- **Row 3d1 closed** (2026-11-23): `@font-face` end to end for local files — parsed, registered, selected by
  `font-family`, and drawn by the pen. `Font`/`FontMetrics` carry the face instead of every path calling
  `active_font()`. An `http(s)` or `data:` source is not fetched yet. (Was: **Row 3d split and started**: `@font-face` is now parsed into `CssStyleSheet::font_faces` (family
  plus the url from `src`), and the glyph cache keys on `(font identity, glyph, size)` so a second font cannot be
  served the first one's bitmaps. What remains for 3d1 is fetching the source and making `font-family` select a
  face — `active_font()` is the only font the paint path can reach today. 3d2 (transitions) and 3d3 (animations)
  are separate rows now, because they are separate features.
- **Row 3d2 closed** (2026-11-23): transitions run. The curve (`ease(0.5) = 0.802403`, ported from upstream's
  cubic-bezier solve), the arithmetic (`Transition::value_at`), and a clock — `RenderDocument::set_time()` plus
  `start_opacity_transition()`, with paint rebuilding the render tree from the existing layout so a transition
  **repaints without reflowing** (measured: red to white in five steps, rect unchanged, `layout_passes()` stays 1).
  What is missing: the trigger (a style change starting one) and every property but `opacity`.
- **Row 4d closed** (2026-11-23): `tools/check-build.py` fails when ninja has pending steps or when a source is newer
  than the binary, and names the newer files. It exists because twice in one session a build was killed and the next
  suite passed on the previous binary. Run it before trusting a green suite: `python3 tools/check-build.py`.
- **Row 1g4c closed** (2026-11-23): `history.scrollRestoration`, kept per history entry the way upstream keeps it on
  the session history entry (`HTML/History.cpp:229-242`). The app records the scroll before a traversal, and the load
  path applies the restore instead of always scrolling to zero. `AetherisBrowserSession` pins both modes and the
  per-entry ownership.
- **Row 5c closed** (2026-11-23): mixed content. The policy and its enforcement already existed, so this row was a
  measurement — and the measurement found two things: trustworthiness was https-only, which blocked loopback
  resources as mixed content and never treated `http://localhost` as secure; and the test was written with `assert`,
  which disappears under NDEBUG. Both fixed; `AetherisSecurityPolicy` now pins 18 cases with `require`.
- **Row 5a closed** (2026-11-23): CSP fetch directives. The policy left `main.cpp` for
  `src/rendering/content_security_policy.{hpp,cpp}`, and `SecureResourceLoader` — the seam every fetch already passed
  through — now maps a request context to its directive and refuses on a mismatch. Source lists cover `'self'`, `*`,
  `'none'`, scheme, host, subdomain and port; a path in a source is not matched, and nonces, hashes and header
  delivery are not implemented — all three are in the row.
- **Row 1g4b closed** (2026-11-23): `pushState`/`replaceState`/`history.state`, and the `window.history` binding that
  row 1g4c owed. `BrowserSession::push_state` resolves the URL against the document, refuses a cross-origin one, and
  never loads anything; the state is JSON rather than a structured clone, and a value JSON cannot carry becomes
  `null` — the same substitute upstream makes when its serialiser refuses.
- **Row 3d3 closed** (2026-11-23): `@keyframes` animations. The rule is parsed into the stylesheet, the `animation`
  shorthand is read, and `Animation` samples a numeric property at the document clock. Unlike a transition, an
  animation starts by itself: `RenderDocument` walks the DOM once when it is built. Measured — a red box fades to
  white over one second in five steps, holds the last keyframe after that, and `layout_passes()` never moves.
- **Row 5b closed** (2026-11-23): same-origin enforcement. Measured first — the engine has no `fetch`, no
  `XMLHttpRequest` and no frames, so there is no cross-origin-capable binding at all. Storage is the only surface, and
  it had a real bug: `https://A.test` and `https://a.test` got separate buckets because the origin key was computed by
  hand in `main.cpp`. It is now lowercased in `set_storage_origin()` and computed by `SecurityPolicy::origin()`, so the
  engine has one definition of "origin". `AetherisJs` pins the isolation.
- **Row 4.1 closed** (2026-11-23): the tab strip. `src/ui/chrome_layout.{hpp,cpp}` computes the strip once and both
  the drawing and the input router read it — before this the router had its own copy of every chrome coordinate and
  a comment saying tab activation was not possible yet. Tabs now switch, close and open by click, shrinking from
  190 px to a 56 px minimum as they are added.
- **Row 4.2 closed** (2026-11-23): history and bookmarks. Entries keep the title they had while current, the session
  gained a bookmark list with a toggle, and the panels are built as ordinary documents of real links — so a click
  navigates through the path a page's own links use rather than a second hit-testing path. Ctrl+H, Ctrl+B, Ctrl+D.
  Nothing is persisted to disk; the row says so.
- **Row 1g2 closed** (2026-11-23): realm isolation. A navigation now replaces the JS context and rebuilds the host
  objects, so the previous document's globals, listeners and timers are unreachable while storage survives. Emptying
  the global object does not work — a top-level `var` is non-configurable — which is why the realm is replaced rather
  than cleared. **This closes the checklist: 66 of 69 rows done, 1 blocked, 2 out of scope.**
- **Row 0.1 advanced, and it found the engine's biggest problem** (2026-11-23): the corpus tool exists and runs
  (`tools/run-corpus.sh`), and `--render file:///…` works now that the CLI registers the file backend. The corpus
  itself is blocked by the sandbox's egress allowlist — only a couple of hosts answer — but the two reachable sites
  were enough: wikipedia's 417 KB does not finish in 280 s, layout costs ~0.8 ms per element on synthetic pages, and a
  2.5× larger document took 9,000× longer to lay out. Rows **3j** (layout cost) and **0.3** (no proxy support) came out
  of it. Full numbers in `docs/COVERAGE-CORPUS.md`.

## 2. Current state

### Works

- HTML5 parsing (gumbo) → DOM, with scripts, styles and implied tags
- CSS: cascade, specificity, media queries **including `calc()` over px lengths in a feature (2026-11-27)**,
  structural pseudo-classes,
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
- Layout: flex and grid, with a flex item's base size from its content (`max_content_width`), a shrink
  floored at its automatic minimum size (`min_content_width`), a row's intrinsic width as the sum of its
  items, **a run of collapsible white space generating no flex or grid item** (css-flexbox-1 §4,
  2026-11-26), **an inline non-replaced box's vertical padding not raising the line** (css2 §8.1,
  §10.6.1, 2026-11-27), and **the space at a text run's edge kept between two inline items and dropped at the
  end of a line** (css-text-3 §4.1.1, 2026-11-27)
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

## 7b. Row 3j — where the style pass goes, and what is left of it

Measured 2026-11-23 with `tools/layout-cost.cpp`, which times parse, style resolution and geometry separately.

- **Style resolution dominates, not geometry.** At 400 empty divs, style was 392 ms against layout's 34 ms, and
  style scales with **rules x nodes** - 10.6 us per (rule x node) before the fix.
- **Fixed:** selectors are parsed once (`src/rendering/selector.cpp`, `CssRule::compiled`), the style fingerprint
  is cached (`StyleProperties::fingerprint`, invalidated by `set()`), and the class test scans the attribute in
  place instead of building an `istringstream` per rule x node. Style is now **2.4-5.4x faster** and the whole
  suite runs in 140 s instead of 400 s.
- **Still open, and measured:** a resolved style holds **17 properties**, and resolving an element costs
  0.114 ms of which **0.074 ms (65%) is property work** and 0.040 ms (35%) is matching. Reserving the map was
  tried and was **33% slower** on an alternating build (0.064 ms vs 0.048 ms), because text nodes, generated
  boxes and the temporaries inside `apply_declaration` all build a `StyleProperties` and most hold one or two
  properties. Reverted. The real fix is structural - a typed property set or interned ids - and it touches
  every consumer of `StyleProperties`.
- **Re-measure before trusting a number.** The 2-core pod is noisy: the same 200-rule case gave 1252 ms and
  3690 ms on different runs. The direction survives the noise; the constant is good to about ±2x.

---

## 7c. Network — the client goes through a proxy

- **Read from the environment, never hard-coded:** `https_proxy`/`HTTPS_PROXY`, `http_proxy`,
  `all_proxy`/`ALL_PROXY`, and `no_proxy`/`NO_PROXY`. `HTTP_PROXY` is deliberately ignored, as
  upstream ignores it: a CGI environment can set it from a request header.
- **https is tunnelled**, http is sent in absolute form. `Proxy-Authorization` goes on the CONNECT for
  https and on the request itself for http. Credentials are never printed — errors use
  `ProxyEndpoint::describe()`, which is `scheme://host:port`.
- **A local fake proxy is how this is tested** (`AetherisProxy`), because it records the bytes the
  client sent. Assert on the wire, not on the code's intentions.
- **`TlsStream::connect_over()`** handshakes over an already-connected socket, working on a duplicate
  of the descriptor. Anything that transfers ownership of an fd has to compile in the no-OpenSSL
  branch too, where `unistd.h` is not included.
- **Still open:** an `https://` proxy, SOCKS4/5, and any `Proxy-Authorization` scheme but Basic.

---

## 7d. Measure in the build type you mean

Every number in rows 3j and 3k started in a Debug `-O0` build with UBSan, which is **14x** slower on
resolve and **93x** slower on the style pass than `-O2`. Before drawing a conclusion about cost,
build `build-rel` (`ninja -C build-rel AetherisRendering`, 137 targets instead of 283) and measure
there.

- **At `-O2`** a 400-div page is 4.2 ms of style and 1.6 ms of layout — 14.5 us per element. The
  "0.8 ms per element" that row 3j was written about was the build, not the engine.
- **The real defect on real markup is layout**, not style: wikipedia at 14 KB lays out **1.16M
  nodes for 134 elements** and takes 5.7 s. Row 3k carries it.
- **Probes print nothing when they are broken.** `layout-cost.cpp` ignored a single file argument for
  a whole session because its loop was `i + 1 < argc`. Read the probe's output, not its exit code.

---

## 7e. Layout — measure once, place once

`LayoutEngine::layout_inline_children` used to lay every child out twice: once at the whole line to
find its natural width, once to place it. That is 2^depth work on a nested document — 40 elements
nested 12 deep cost 22,526 layout steps, and a 14 KB wikipedia prefix cost 1.16M.

- **Only inline-level children need measuring.** A block fills the line, so its natural width is the
  available width and measuring it lays out a subtree for nothing.
- **A fitting child needs no second layout.** It was measured at this origin with a whole line to
  fill; a shrink-to-fit box given more room than it needs produces the same geometry.
- **When a test disagrees with the engine, check which one is wrong.** The first version of the
  regression test asserted that each nested block sits below its parent; the engine legitimately
  places a nested block on its parent's line, so the assertion was rewritten.

---

## 7f. A pod that recycles costs you the toolchain, not the build

The sandbox is a new container most days. What survives is `/scratch/work` — the repo and both build
directories. What does not is everything `pip` installed, so `toolchain/zigcc` points at a zig that is
gone and every probe fails with `exec: .../zig: not found`. Reinstall and re-point before anything else:

```sh
pip install -q ziglang cmake ninja
ZIG=$(python3 -c 'import ziglang,os;print(os.path.join(os.path.dirname(ziglang.__file__),"zig"))')
printf '#!/bin/sh\nexec %s cc "$@"\n'  "$ZIG" > /scratch/work/toolchain/zigcc
printf '#!/bin/sh\nexec %s c++ "$@"\n' "$ZIG" > /scratch/work/toolchain/zigcxx
```

The CMake cache does **not** survive a recycle either, so the first `ninja` after one rebuilds all
300 steps. `tools/check-build.py` says so; believe it rather than a directory that has binaries in it.

---

## 7g. Measure coverage, don't guess at gaps

`tools/css-coverage.cpp` answers "which declarations does this engine act on?" by resolving one element
twice per declaration and comparing `fingerprint()`.

- **The properties are not the problem.** 98.3% of wikipedia's 2,869 declarations change the resolved
  style. The rest set a property to its own initial value, which cannot be seen that way.
- **The rule count is the problem.** A real stylesheet parsing to 7 rules means the stylesheet is not
  reaching the engine, not that properties are missing. Check `rules.size()` before writing a row about
  property support.
- **Two traps found that way:** classifying a stylesheet by its `.css` suffix (a page served from
  `load.php?...&only=styles` has none, so its 41 stylesheets were cached as images), and letting
  `@supports`/`@layer`/`@container` fall through to the rule path, where the condition became a selector
  and everything after the block was lost.
- **A number already in the output was the clue**: "12330 bytes of CSS" for a 417 KB page.

`tools/setup.sh` rebuilds the toolchain and both build directories after a pod recycle; `--check` reports
what is missing without changing anything.

---

## 7h. White space is not an item and has no width

Two rules, both from the spec, both of which were missing and both of which show up on every real page
because real markup is indented.

- **css-flexbox-1 §4**: a contiguous sequence of child text runs becomes an anonymous flex item *unless the
  whole run is white space, in which case it is not rendered at all*. Upstream's predicate is
  `can_skip_is_anonymous_text_run` (`Rust/src/layout/layout_pass.rs:455`), applied at
  `flex_formatting_context.rs:1217` and `grid_formatting_context.rs:2180`. Here it is
  `is_collapsible_whitespace_run()` in `layout.cpp`, used by both item collectors. Before it, a newline
  between two boxes was an item with a width **and** a gap on each side.
- **The minimum width of a text run** is its widest word, and a break opportunity exists at **every**
  collapsible space, not only at U+0020. `min_content_width` split on `' '` alone, so `"\n\t\t\t"` was one
  unbreakable word and a box that renders nothing had a 28.83px floor that every ancestor inherited.

- **The maximum width of a text run** is the same rule from the other side. A run of collapsible white space
  is one space, not as wide as the characters that write it, and inside a container that lays out a row of
  items it adds nothing at all - the same rule as the minimum, and as the item collectors. Before this,
  `"\n"` measured 9.61px and `"\n\t\t\t"` 28.83px, so every base size carried its markup's indentation:
  a header's `nav` measured **615.42px where 423.23px was content**, the row was over-full, `flex-grow` on
  the search box never applied, and the account links were pushed to the left of a header with room for them.

The instrument that found all three: `tools/article-position.cpp` prints `max=` and `min=` for every node,
and `--all` prints every offender rather than the worst fifteen. One number cannot say whether an item is
bigger than its base or floored above the space it was given, and the first is a layout bug while the second
is a sizing bug with a different fix; a count that moves by two cannot be explained by a list of fifteen.

**Still wrong, as rows 3n and 0.14:** a lone flex item is never shrunk to its minimum because its base is
capped at the line width; and the page title bar is painted black and placed ~410px below where a browser
puts it, which is the worst band of the reference render by a factor of two.

---

## 7i. A box-shadow is a list, and an outer shadow stays outside its box

- **css-backgrounds-3 §7.2.** `box-shadow` is a comma-separated list; `none` — the initial value — is the empty
  list; each shadow is two to four lengths, an optional `inset` and an optional colour that defaults to
  `currentColor` and may appear **anywhere** in the list. Upstream keeps exactly that shape: a list of
  `ComputedShadow` values, each with its own colour and placement, empty for `none`
  (`Rust/src/css/computed_value_types.rs:1052`, `:1069`).
- **Reading it positionally as `x y blur colour`** with a black default turned `box-shadow: none` into an opaque
  black rectangle the size of the box — a 976x78 black band across a real page's title bar — and turned a light
  `0 1px #c8ccd1` into the same thing, because the colour landed in the blur slot.
- **§7.2.1: an outer shadow is not painted inside its border box.** Filling the whole offset rectangle put a grey
  band across a toolbar whose box has no background of its own. `SoftwareSurface::draw_shadow` now fills four
  strips around the border box.

Fixing both moved the reference render **80.4% -> 88.8%**, the largest single step since the reference existed.

---

## 7j. A grid written with the `grid-template` shorthand has no tracks at all

- **css-grid-1 §7.3**: `grid-template` is a shorthand for `grid-template-rows`, `grid-template-columns` and
  `grid-template-areas` — the row list (with any quoted area strings interleaved) before a `/`, the columns
  after it. Upstream states exactly those three longhands (`CSS/Properties.json:3282-3297`).
- **This engine has no shorthand-expansion layer.** A shorthand is read where it is used, the way
  `resolve_edges` splits `margin`. So `layout_grid_children` falls back to the shorthand for whichever longhand
  is absent, splitting on the **top-level** `/` (never one inside `minmax(...)`), and `parse_tracks` reads
  `minmax(a, b)` as the track its second argument names.
- **The tell**: a container reporting `display=grid` whose children are all stacked in one column. Fixtures:
  longhand `grid-template-columns:100px 1fr` puts two `grid-area` children at x=0 w=100 and x=100 w=924; the
  shorthand `grid-template: 1fr / 100px 1fr` used to put them at x=0 w=1024 and x=1024 w=0.

**And a lesson about premises.** Row 0.15 was opened saying a real page's table of contents was wrongly in the
content flow. It is not: at 1024px chromium stacks the Contents list above the title too, because the grid that
would put it in a column is behind `@media screen and (min-width:1120px)`. The row's *number* was right — the
article starts 190px below chromium's — and its *explanation* was wrong, which is worse than no explanation.
Compare the two renders before believing a layout story about them.

---

## 7k. A percentage height is not a percentage of the width

- **css2 §10.5** (and css-flexbox-1 §9.8 for a flex item): a percentage `height` resolves against the containing
  block's **height**; against an indefinite one it computes to `auto`.
- The engine passed the element's own **width** as the percentage base — a different quantity — so
  `.mw-logo { height:100% }` on a 140px-wide logo became **140px tall**, stretched the site header from ~68px to
  156px and pushed the article down. `parse_height()` now returns the fallback for any percentage.
- **Fixture**: a 38px span inside `<a style='display:flex;height:100%'>` in an auto-height flex row made the row
  **100px** tall; it is 38px now. On the page `main#content` moved **546.4 -> 473.6** and the engine's text rows
  went from 88px below chromium's to 15px below.

**And a reminder about the agreement percentage.** That fix made the aggregate fall, 88.8% -> 84.3%, because moving
everything up 73px pulled the *next* defect into the viewport: the article ends ~70px early, so the footer lands on
screen where chromium still has paragraphs. The row offset improved and the percentage got worse. Read the row
positions first, every time.

---

## 7l. An inline box's vertical padding paints outside the line and does not move it

- **css2 §8.1 / §10.6.1**: the vertical padding, border and margin of an **inline, non-replaced** box start at the
  top and bottom of its content area and have *nothing to do with* the line-height. Only the line-height is used
  when calculating the height of the line box. The padding paints; it does not advance the next line.
- The engine added `child->rect.height` — the whole border box, padding included — to the line's `max_bottom`. A
  page's table of contents is `<a><div class="vector-toc-text" style="padding:6px 0">`, so every row came out
  **28.8px** where a browser gives **16.8**, and the list pushed the article down.
- `layout_inline_children` now contributes `max(line_height, content height)` for an inline non-replaced child, and
  keeps the full border box for an **inline-block or a replaced element**, which are atomic and do move the line.
- **Fixture, measured both ways**: two block `<a>` each holding a `padding:6px 0` span — gap **28.80 -> 16.80**,
  block height **57.60 -> 33.60**.
- **This moved no page-level number, and that is the correct outcome**: agreement 84.3% -> 84.1%, engine text rows
  byte-identical, overflows steady at 139. A compatibility fix that makes the engine agree with a browser on a
  construct the page uses is still worth its commit; it just does not show up in a whole-page percentage.

**A note on what a percentage can and cannot say.** Four rows now have moved agreement the wrong way while the
layout got better (0.11, 0.15, 3m, and this one). The percentage is dominated by whichever single defect is
biggest. Print the per-band table and the row positions *alongside* it, and never read the number alone.

---

## 7m. A media query can be silently false, and `calc()` is how

- **css-values-4 §10**: a `calc()` in a media feature is a math function over lengths, and it is **absolutized before
  the comparison**. Upstream does exactly that — `with_tree_counting_resolved` → `css::calc::absolutize_calculation_value`
  against the query's own length context (`Rust/src/css/parser/query_parser.rs:2111-2131`).
- The engine read the digits before the first `"px"`: `value.substr(0, value.find("px"))`. `max-width: calc(1120px - 1px)`
  became `"calc(1120"`, `strtof` failed, and the query returned false. **A whole rule never applied and nothing said so.**
  The close paren was also `rfind(')')`, the *last* one, which on a `calc()` value is the wrong one.
- `LengthExpression` is a recursive-descent evaluator: `expr := term (('+'|'-') term)*`,
  `term := factor (('*'|'/') factor)*`, `factor := number unit? | '(' expr ')' | '-' factor`. Units: `px`, bare number,
  `em`/`rem` at the initial 16px (a media query has no element to inherit from, css-values-4 §5.1.1), `pt`, `pc`, `in`,
  `cm`, `mm`, `q`. `min()`/`max()`/`clamp()` are **rejected**, not guessed — an unreadable feature must not be mistaken
  for zero, because `max-width: 0` would match everything.
- **The parser strips all whitespace before `media_matches` sees the text**, so `calc(1120px - 1px)` arrives as
  `calc(1120px-1px)`. That is why `factor()` accepts a sign only at the start of a term and never between a unit and the
  next operator.
- **Scope, measured**: 76 `@media` blocks in the page's CSS, 17 distinct preludes, **13 blocks change state, 29 rules**,
  and all 13 are `calc()` preludes. 63 of 76 evaluate identically — that is what makes the fix narrow. One of them,
  `min-width: calc(640px - 1px)` = 639, matches at 1024, so this was never only a table-of-contents cap: a block of
  "desktop-and-up" rules had never applied either.

**A media query is the one failure mode that leaves no trace.** A node that reports `display: block` where the CSS says
`grid` is a selector or media-query failure, not a layout failure. Check that the rule applied before writing a row
about what the layout did with it.

---

## 7n. The space at the edge of a text run

- **css-text-3 §4.1.1**: a collapsible space is kept when it sits **between two inline items on a line**, and removed
  when it is at the **end of a line**.
- `TextLayout::layout` only ever emits a space **between two words of its own run** — `text_layout.cpp:116` discards it,
  `:137` puts it back. So a run that starts or ends in a space loses it, and `layout_inline_children` placed the next
  inline box at `cursor_x = rect.x + rect.width`, flush against the last word: `"often abbreviated as"` + `<a>browser</a>`
  read **"asbrowser"**.
- It is **symmetric**. The article body has **424** boundaries of whitespace before an inline start tag and **316**
  after an inline close tag — 740 in total. A trailing-only fix leaves 316 of them wrong.
- The text branch now advances `cursor_x` for a leading space when `cursor_x > x`, and for a trailing space when the
  next in-flow non-positioned sibling is inline-level and `cursor_x > x`. `cursor_x > x` is the same "something is
  already on this line" test the wrap decision already used; the sibling test is what keeps the end-of-line case right.
- **Each half was proved to fail on its own** — with the whole fix reverted the trailing assertion goes red, and with
  only the leading advance disabled the leading assertion goes red. A test that cannot fail is not proof, and a test
  behind another failing assertion has not been shown to fail at all.

**The one thing to watch:** the trailing advance grows the text node's `rect.width` by one space, which feeds an inline
ancestor's shrink-to-fit. If a render moves in a way the fix does not explain, drop the `child->rect.width +=` line
first and see whether the `cursor_x` advance alone is enough.

---

## 7o. Floats

- **css2 §9.5**: a floated box is **taken out of the normal flow**, shifted to the **left or right edge of the line box**
  it is in, and the content that follows **flows beside it** rather than below it. Before row 0.20 the engine never read
  the property at all, so `figure.mw-halign-right` laid out as an ordinary block at **x=46.4 w=953.6** on a 976px
  column and covered the article.
- **The shape.** `struct FloatBox { float top, bottom, left, right; }` and
  `static void float_intrusion(std::vector<FloatBox> const&, float content_left, float content_right, float y, float height, float& left_inset, float& right_inset)`
  sit in `src/rendering/layout.cpp`, just above `LayoutEngine::layout_inline_children`. A `FloatBox` is a placed
  float's **margin box**; `float_intrusion` walks the list and, for every box that overlaps `[y, y + height)`, reports
  the inset it causes on whichever side it touches (`box.left <= content_left` means it hangs off the left edge;
  `box.right >= content_right` means the right). A float that touches neither edge is ignored, which is why floats do
  not stack.
- **Where the float branch lives.** Inside `layout_inline_children`, after the `display: none` / positioned skip:
  `if (auto const* side = child->style.get("float"))`, and for `left`/`inline-start` or `right`/`inline-end` the child is
  laid out at `min(max_content_width(child), available_width)`, re-laid at `content_right - outer` when floated right,
  its margin box pushed into the local `std::vector<FloatBox> floats`, and the loop `continue`s **without advancing the
  flow**. `float const content_right = x + available_width` is computed once, before the child loop.
- **Who consumes the bands.** Both the block-level child path and the text-run path call `float_intrusion(...)` and start
  beside the float: the block path at `x + block_left` with width `available_width - block_left - block_right`; the text
  path raises `cursor_x` to `x + run_left` and narrows the run limit to `content_right - run_right`.
- **The routing edit, which is the part that was never measured until 2026-11-27.** `layout_node` scans its children
  for a float into `has_float`, and dispatches `else if (has_inline || has_float) layout_inline_children(...)`. Without
  `has_float`, a block container holding only blocks plus one float takes the plain block-stack path, where the float is
  placed as an ordinary block and the bands are never built - so the whole mechanism is dead on exactly the containers
  that need it.

**The three gaps, plainly.** These are known and not hidden:

1. **`clear` is not implemented.** Nothing reads `clear: left | right | both`; a box that should be pushed below a float
   is placed beside it instead.
2. **Floats do not stack.** `float_intrusion` takes the inset from a box only when it touches the container's left or
   right edge, and every float is placed at the current `cursor_y` against that edge. Two floats on the same line both
   claim the full band rather than sitting one beside the other, and a second float does not drop below the first.
3. **Intrusion is per-block, not per-line.** The inset is computed once when the block or text run begins and applies to
   the whole of it, so a `<p>` that starts beside a float has **all** of its lines shortened, including the lines below
   where the float has already ended. css2 §9.5 asks for the inset to be recomputed per line box.

**Measured on `Web_browser` at 1024px:** agreement **84.8%** (41672/49152 exact-match samples every 4px; 84.1% before),
the figure at **x=772.4 y=468.6 w=227.6 h=179.6** with its right edge on 1000.0, chromium's own edge. Band 576-640
recovers **27.7% -> 80.4%**. The worst band is now 640-704 at **50.5%**, and the gaps above are the likeliest cause.

---

## 7p. Floats shorten line boxes, and upstream makes the line the unit

- **css2 §9.5**: a float is out of flow and sits against the left or right edge of its containing block. A block's
  **border box still overlaps it**; only the **line boxes** that intersect it are shortened. Narrowing the block
  instead is the easy approximation and the wrong model - it is what this engine did until row 0.20, and the
  assertion in `AetherisAdvancedLayout` that checks `after.rect.width < 150.0f` still encodes it.
- **Upstream makes the line the unit of inline placement.** `LineBuilder::begin_new_line` calls
  `recalculate_available_space` (`Rust/src/layout/line_builder.rs:2297-2304`), which asks
  `available_space_for_line(block_offset, line_block_size)` (`Rust/src/layout/inline_formatting_context.rs:1958-1970`):

  ```rust
  let intrusions = self.intrusion_by_floats_into_containing_block(block_offset, block_offset + line_block_size);
  AvailableSize::definite(self.input.available_space.inline_size.to_px_or_zero() - intrusions.left - intrusions.right)
  ```

  So the width available to a line is a **per-line query against the float context**, recomputed on every new line.
- **A line with no room jumps past the float.** `begin_new_line` takes `next_float_band_block_start_after`
  (`line_builder.rs:358-362`) and sets the next line's block offset to the next band start, rather than stepping by a
  line height into a gap nothing can occupy.
- **The bands are a sorted step function, not rectangles.** `FloatBand { block_start, left_intrusion: Option<..>,
  right_intrusion: Option<..> }` (`Rust/src/layout/block_formatting_context.rs:1190-1197`) - each band's intrusions
  hold from its `block_start` until the next band's. `None` means no float on that side. A float can intrude by zero or
  a negative amount and must still push later floats on the same side.

**Why this matters to this engine.** Its unit is the **run**, not the line: `TextLayout::layout` wraps a whole text run
against one width. That single choice produces every failure row 0.21 has had:

| symptom | why the run model causes it |
| --- | --- |
| per-line intrusion needs a `std::function` or a band array threaded into the wrapper | the wrapper has no concept of a line box |
| a run that starts mid-line wrapped its continuation lines at its own x (x=708.5, one word per line) | there is no line box to start from |
| a word that does not fit the shortened line overflows under the float instead of dropping a line | within a run the wrapper only breaks at the room *it* computed, and a run that begins mid-line has no line to break into |

**The conclusion, and it is the reason three attempts were reverted:** the fix is not another patch to `TextLayout`.
It is to move the wrap loop out of `TextLayout` and into `layout_inline_children`, one line at a time, asking the bands
for the room at each line and skipping to the next band start when a line has no room - which is the shape of
upstream's `LineBuilder`. Build it against a fixture first; the page cannot tell you which of three interacting
mechanisms is wrong.

---

## 7q. A generic family has no face, and the reference browser has its own stack

- **`font-family: sans-serif` is not a font, it is a request to the platform.** css-fonts-4 §2.1: the generic
  families are keywords the UA resolves against what is installed. This engine resolved nothing: `font_for_family`
  looked in its own face registry and then returned the embedded face, so a page whose `body` says `sans-serif` -
  which is nearly every page - was set in the embedded **DejaVu Sans** regardless of the machine.
- **On Linux the mechanism is fontconfig, and that is upstream's position, not a preference.** `SystemFontMatcher.h:44`
  declares `resolve_generic_family` as "The installed family for a generic family, such as 'sans-serif'", and
  `SystemFontMatcherDirectWrite.cpp:264-266` notes that DirectWrite "has no families for the generic families,
  unlike fontconfig" - which is why the hardcoded table lives in the Windows backend. The hardcoded table is the
  exception; the fontconfig answer is the rule.
- **This engine links no fontconfig**, so `font_library.cpp` names the files fontconfig answers with, in preference
  order, and caches the load under the generic's own name:

  | generic | first candidate | second candidate | embedded fallback |
  | --- | --- | --- | --- |
  | `sans-serif`, `system-ui` | `/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf` | `.../liberation/LiberationSans-Regular.ttf` | DejaVu Sans |
  | `serif` | `/usr/share/fonts/truetype/noto/NotoSerif-Regular.ttf` | `.../liberation/LiberationSerif-Regular.ttf` | DejaVu Sans |
  | `monospace` | `/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf` | `.../liberation/LiberationMono-Regular.ttf` | DejaVu Sans |

- **The list is a fallback chain only if it is tried, not probed.** The first version returned the first candidate
  that *existed* and loaded only that one, so a present-but-unparseable file dropped straight to the embedded face
  without ever reaching Liberation. Every candidate is now attempted until one loads. A failure is cached too: the
  files are ~500KB and were being read and parsed again on every text run that named a missing family.
- **Measure the face, don't infer it.** The numbers below are the rightmost dark pixel on a line of
  `A web browser, often abbreviated as browser, is an application` at 14px with `white-space: nowrap`, in both
  engines at 1024x768; the advances are PIL's for the same string at 14px:

  | family | chromium ink | engine ink (after) | PIL advance | face |
  | --- | --- | --- | --- | --- |
  | `sans-serif` | **385** | **405** | Noto Sans 406.83 | engine: Noto Sans · chromium: Liberation Sans |
  | `serif` | **349** | **416** | Noto Serif 417.95 | engine: Noto Serif · chromium: Liberation Serif |
  | `monospace` | **519** | **520** | DejaVu Sans Mono 522.16 | both: DejaVu Sans Mono |
  | `'Noto Sans'` | 405 | 440 | 406.83 | engine has no by-name face - embedded |
  | `'Liberation Sans'` | 385 | 440 | 386.66 | engine has no by-name face - embedded |
  | `'DejaVu Sans'` | 439 | 440 | 441.61 | embedded |
  | `'Noto Serif'` / `'Liberation Serif'` / `'Times New Roman'` | 416 / 349 / 349 | 440 | 417.95 / 350.77 | chromium: its own default names |
  | `Arial` / `Helvetica` | 385 / 385 | 440 | Liberation Sans 386.66 | fontconfig substitutes Arial to Liberation Sans |

- **So the engine measures Noto Sans exactly, and the residual difference is the reference browser's face choice.**
  `sans-serif` is 405 ink against a 406.83 advance - the last glyph ends 1.8px before its advance, which is what
  "exact" looks like - and chromium's own `'Noto Sans'` line measures 405 too. The 20px is entirely that chromium
  reaches `sans-serif` through its **default family name Arial** (385 = chromium's `Arial` = chromium's
  `'Liberation Sans'`) and `serif` through **Times New Roman** (349 = its `'Liberation Serif'`), while agreeing with
  fontconfig on `monospace`. That is a private font stack, not fontconfig's answer, and copying it is not what a
  browser on Linux should do. Recorded, not chased.
- **What it buys, and where.** Reference page = wikipedia `Web_browser`, self-contained, 1024x768, agreement sampled
  every 4px: `main` **84.8%** (41672/49152), the font fix alone **85.2%** (41885/49152), and with row 0.21's branch
  underneath **85.3%** (41924/49152). The gain is small on the page because most of the page's text is already in
  faces it names; the change is worth landing because the *default* face is now the machine's, which is what makes
  any further font work mean anything. Bands y320-y768 move most (y704 82.2 -> 90.3); y448 is the one band that goes
  down (82.5 -> 79.4) and it is a face choice, not a placement error - the text there is now set in a face whose
  line breaks differ from the reference's.
- **Two things deliberately not done.** System families are still not registered by their real names, so
  `font-family: 'Noto Sans'` on a page still lands on the embedded face; only generic names resolve. And nothing here
  reads the machine's font configuration - a machine with no Noto and no Liberation still gets DejaVu, silently.


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

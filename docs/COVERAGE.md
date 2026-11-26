# Coverage — what this engine does and does not do

**Snapshot, 2026-11-23.** Every row was checked against the code on that date; the evidence column says
where. This is a *snapshot*, not an instrument: it does not move on its own, and it says nothing about how
many real pages are affected by each gap.

## Why this file exists

A review of `ROADMAP.md` found that its ordering rested on guesses about which gap costs the most pages.
The fix is an instrument — render a fixed corpus, count what breaks — and this file is the honest stand-in
until that exists. **The corpus step is blocked in the sandbox this was written in**: it has no open
internet, only allowlisted hosts, so twenty real sites cannot be fetched. The commands to run it elsewhere
are at the bottom.

## Markup and CSS

| Feature | State | Evidence |
| --- | --- | --- |
| HTML5 parsing (gumbo) | done | `third_party/gumbo` |
| HTTP and HTTPS through an environment proxy (`http_proxy`, `https_proxy`, `all_proxy`, `no_proxy`) | done, 2026-11-23; CONNECT for https, absolute form for http | `include/aetheris/network/proxy.hpp`, `AetherisProxy` |
| CSS cascade, specificity | done, incl. `:is`/`:not` greatest-argument and `:where` zero | `src/rendering/style.cpp` |
| Compound, descendant and child selectors | done, and parsed once per selector since 2026-11-23 | `matches_compound()` in `style.cpp`, `selector.cpp` |
| Sibling combinators `+` `~` | done, 2026-11-18 | `matches()` walks previous element siblings |
| Attribute selectors (6 forms) | done | `style.cpp` |
| `[a=v i]` case flag | done, 2026-11-18 | `AttributeRequirement::insensitive` |
| `:hover` `:focus` `:active` `:focus-visible` `:focus-within` | done | `visual_state.hpp`, `style.cpp` |
| `:first-child` `:last-child` `:only-child` `:nth-child(n)` + An+B | done | `matches_structural_pseudo()` |
| `:nth-last-child`, `-of-type` family | done | same |
| `:empty`, `:root` | done | same |
| `:is()` `:where()` `:not(list)` `:has()` | done | `matches_simple()` |
| `:checked` `:disabled` `:enabled` `:placeholder-shown` | done, 2026-11-18 | attribute facts in `matches_simple()` |
| `:lang` `:dir` `:scope` | **absent** | no handling |
| `::before` `::after` and other pseudo-elements | done — synthetic boxes; text, background and border all paint, and the box is content-sized (2026-11-23) | `layout.cpp`, `render_tree.cpp`, `AetherisTextLayout` |
| CSS custom properties `var()` | done, 2026-11-17 — inherited, fallbacks, chains, cycle guard | `substitute_vars()` in `style.cpp` |
| Media queries | done | `css_parser.cpp` |
| `@font-face` | done for local files, 2026-11-23 — parsed into `CssStyleSheet::font_faces`, a `file://`/path `src` is read and registered, `font-family` selects it through `Font`/`FontMetrics`/`draw_text`. Not done: `http(s)`/`data:` sources are not fetched, and weight/style do not pick among a family's faces | `css_parser.cpp`, `font_library.cpp`, `font.cpp`, `render_document.cpp` |
| Transitions | run, 2026-11-23 — timing functions, a `Transition` value model, a document clock, opacity repainting without reflowing. Not done: the trigger (a style change starting one) | `easing.cpp`, `transition.cpp`, `render_document.cpp`, `AetherisEasing` |
| Animations (`@keyframes`) | run, 2026-11-23 — `@keyframes` parsed, `animation` shorthand read, sampling at the document clock, started automatically from the stylesheet. Numeric properties only (`opacity`); no `direction`, `fill-mode`, `play-state` or comma lists | `animation.cpp`, `css_parser.cpp`, `render_document.cpp`, `AetherisEasing` |
| `history.scrollRestoration` | done, 2026-11-23 — per history entry (Auto/Manual), with the offset recorded on the entry being left and restored on traversal when the destination is Auto | `browser_session.cpp`, `main.cpp`, `AetherisBrowserSession` |
| History API | done, 2026-11-23 — `length`, `go`/`back`/`forward`, `pushState`/`replaceState` with a per-entry state, `history.state`, `scrollRestoration` (both the C++ API and the `window.history` binding). State is JSON, not a structured clone | `browser_session.cpp`, `js.cpp`, `AetherisBrowserSession`, `AetherisJs` |
| Mixed content | done, 2026-11-23 — a secure document may not fetch insecure blockable content; images are the optionally-blockable category this engine renders. Trustworthiness is https, wss, file and loopback, per upstream's secure-contexts rule | `security_policy.cpp`, `secure_resource_loader.cpp`, `AetherisSecurityPolicy` |
| Content-Security-Policy | fetch directives done, 2026-11-23 — `img-src`, `style-src`, `script-src`, `connect-src`, `frame-src`, `form-action` with `default-src` fallback, source lists including `'self'`, `*`, `'none'`, scheme, host, subdomain and port; inline scripts and styles gated. Not done: nonces, hashes, report-uri, header delivery | `content_security_policy.cpp`, `secure_resource_loader.cpp`, `AetherisSecurityPolicy` |
| Same origin | done as far as the engine has a surface, 2026-11-23 — storage is keyed on one lowercased origin definition shared with the security policy. No `fetch`, `XMLHttpRequest` or frames exist, so there is no cross-origin binding to refuse yet | `js.hpp`, `main.cpp`, `AetherisJs` |
| Tab strip | done, 2026-11-23 — one layout drives both painting and hit-testing; tabs switch, close and open by click, shrinking from 190 px to a 56 px minimum. No drag-reorder, middle-click, or overflow menu | `chrome_layout.cpp`, `input_router.cpp`, `main.cpp`, `AetherisInputRouter` |
| History and bookmarks UI | done, 2026-11-23 — per-entry titles, a bookmark list with toggle, and panels built as ordinary link documents (Ctrl+H / Ctrl+B / Ctrl+D). No persistence, no editing, no search | `library_page.cpp`, `browser_session.cpp`, `input_router.cpp`, `AetherisBrowserSession` |
| Realm isolation | done, 2026-11-23 — a navigation replaces the JS context and rebuilds the host objects; the previous document's globals, listeners and timers are unreachable, storage survives | `js.cpp`, `main.cpp`, `AetherisJs` |
| Real-page coverage | **first measured**, 2026-11-23 — `example.com` renders; wikipedia's 417 KB does not finish in 280 s. Layout costs ~0.8 ms per element on synthetic pages and far more on real markup. See `docs/COVERAGE-CORPUS.md` and rows 3j, 0.3 | `tools/run-corpus.sh` |
| Flexbox: `flex` shorthand, basis, grow, shrink, `justify-content`, `align-items` | done, 2026-11-26 — an `auto` basis takes the item's own content width, a shrink is floored at the item's automatic minimum size, a row's intrinsic width is the sum of its items, a run of collapsible white space generates **no item** and adds **nothing to either intrinsic width** (one space outside a row of items). Not done: a lone item is never shrunk to its minimum (row 3n), and `flex-wrap`/`align-content`/`order` are unread | `layout.cpp`, `style.cpp`, `AetherisAdvancedLayout` |
| Grid: tracks, areas, explicit placement | done, 2026-11-23 — `grid-template-columns`/`rows` tracks, `grid-template-areas` + `grid-area`, `grid-column`/`grid-row` with `start / end`, and auto-placement into the first free cell. Not read: named lines, `span n`, `auto-fit`/`minmax`, `grid-auto-flow: column` | `layout.cpp`, `AetherisLayout` |
| `transform` (translate/scale) | done | paint pass |
| `position: sticky` | done, 2026-11-23 — shifted at paint time, clamped to the containing block; `top` and `bottom` both read, `top` wins when they conflict | `render_tree.cpp`, `paint_executor.cpp` |
| `visibility` | done, 2026-11-18 — inherited, per-box, `collapse` as `hidden` | `RenderTree::emit()` |

## Layout

| Feature | State | Evidence |
| --- | --- | --- |
| Block flow | done | `layout.cpp` |
| Inline flow, line breaking, `white-space` | done — breaks at whitespace and at the cluster offsets the shaper allows; overflows when there is no opportunity, as a browser does (measured 2026-11-23) | `text_layout.cpp`, `AetherisTextLayout` |
| Flex | done for a single line, 2026-11-26 — basis, grow, shrink, the automatic-minimum floor, gaps, `justify-content`, `align-items`, and white-space runs excluded from the items and from both intrinsic widths. Not done: `flex-wrap` (one line only), `align-content`, `order`, `align-self` | `layout_flex_children()` |
| Grid | partial — `grid-template-columns` read | `layout_grid_children()` |
| Absolute positioning | done | `layout_absolute_children()` |
| `position: sticky` | **absent** | no `sticky` in `src/rendering` |
| `float` | **absent** | not read by layout |
| Incremental layout | done — document skip, per-node reuse on origin | `render_document.cpp` |

## Text and fonts

| Feature | State | Evidence |
| --- | --- | --- |
| TTF parsing | done | `ttf_font.cpp` |
| Glyph rasteriser | done | `glyph_rasterizer.cpp` |
| Rasteriser wired into layout and paint | done — `draw_text` rasterises the glyphs of a shaped run through `cached_glyph()`, asserted to match `rasterize_glyph()` value for value; layout measures the shaped width and the pen advances by the same one | `text_layout.cpp`, `software_surface.cpp`, `AetherisGlyphRasterizer` |
| Kerning | done, 2026-11-23 — a shaped run gets it from GPOS and layout now measures it ("To" 19.5625 -> 16.8594 px at 16 px); the fallback path uses the embedded pair table (A+T = -159), and both are asserted | `font.cpp`, `text_layout.cpp`, `AetherisShaping` |
| Shaping (ligatures, Arabic, Indic) | done, 2026-11-18 — HarfBuzz, `shape_run()`; `measure("fi")` 10.0938 -> 10.0781 | `shaper.cpp`, `AetherisShaping` |
| Shaped-run cache | done, 2026-11-22 — keyed by (font identity, size, letter-spacing, word-spacing, text); a cached run is 0.0001 ms at -O2 | `shaper.cpp`, `tools/run-shaping-bench.sh` |
| `letter-spacing`, `word-spacing` | done, 2026-11-23 — applied to the glyph advances inside the shaper, so layout and paint share one number; the trailing edge keeps its letter-spacing, as upstream and other browsers do | `shaper.cpp`, `text_layout.cpp`, `render_tree.cpp` |
| `letter-spacing`, `word-spacing` | done, 2026-11-23 — applied to the glyph advances inside the shaper, so layout and paint share one number; the trailing edge keeps its letter-spacing, as upstream and other browsers do | `shaper.cpp`, `text_layout.cpp`, `render_tree.cpp` |
| Bidi | **levels 1-2**, 2026-11-22 — classification, run splitting, an explicit direction per run, and the line's fragments reordered into visual order; `direction: rtl` is honoured and inherited. Not done: `unicode-bidi`, the `dir` attribute, embeddings/isolates, and `text-align: justify` | checklist rows 2e, 2e2, 2e3 |
| Line breaking by cluster | done, 2026-11-23 — breaks only at cluster starts the shaper says are safe (`HB_GLYPH_FLAG_UNSAFE_TO_BREAK`), and inside a word only under `overflow-wrap: break-word|anywhere` / `word-break: break-all`. No UAX#14 segmenter: the opportunities are whitespace plus those offsets | checklist row 2f |

## Scripting, storage, network, safety

| Feature | State | Evidence |
| --- | --- | --- |
| JavaScript (QuickJS), events, timers on virtual time | done | `src/main.cpp` |
| `requestAnimationFrame`, `performance.now()`, `getComputedStyle` | done | same |
| `localStorage` / `sessionStorage` | **absent** | no bindings |
| Cookies | done | `cookie_jar.cpp` |
| TLS certificate verification | done and proven | `Tools/check_tls_verification.sh` |
| HTTP version coverage | **unverified** — no version strings in the resource backend | `http_resource_backend.cpp` |
| CSP: `<meta>` policy, inline-script gating | done | `main.cpp` (`content_security_policy`, `csp_allows_inline_scripts`) |
| CSP: fetch directives (`img-src`, `connect-src`, …) | **absent** | — |
| Same-origin enforcement in the bindings | **absent** | — |
| Process/site isolation | **out of scope** | `ROADMAP.md` |

## Shell

| Feature | State | Evidence |
| --- | --- | --- |
| Window layer (SDL2 + headless), chrome bar, tabs counter | done | `src/ui/` |
| Tab strip, bookmarks, history UI | **absent** | — |
| Accessibility tree, devtools | **absent** | — |

## Filling this in with real pages

Where there is network access, the instrument is:

    for url in $(cat corpus.txt); do
      ./build/bin/aetheris --render "$url" "/tmp/$(echo "$url" | tr / _).ppm" 2>"$url.err"
      ./build/bin/aetheris --window "$url" 3 | tail -1     # relayouts, layouts, resolves, …
    done

| Column to record | Where it comes from |
| --- | --- |
| renders / blank / error | the PPM and the stderr |
| JS errors | `[js]` lines on stderr |
| selector and layout gaps | the `.err` file plus this table |
| cost of the first pass | `layout nodes:` in the window summary |

Twenty sites is enough to order the gaps. Do that before starting anything in the fonts row — it is the
largest single piece of work on the roadmap and the easiest to spend a month on for pages that would not
have used it.

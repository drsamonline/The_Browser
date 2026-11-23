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
| CSS cascade, specificity | done, incl. `:is`/`:not` greatest-argument and `:where` zero | `src/rendering/style.cpp` |
| Compound, descendant and child selectors | done | `matches()` in `style.cpp` |
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
| Transitions, animations | **transitions run**, 2026-11-23 — timing functions, a `Transition` value model, a document clock and an opacity transition that repaints without reflowing. Not done: the trigger (a style change starting a transition), and every property but `opacity`. Animations (`@keyframes`) are row 3d3 | `easing.cpp`, `transition.cpp`, `render_document.cpp`, `AetherisEasing` |
| `history.scrollRestoration` | done, 2026-11-23 — per history entry (Auto/Manual), with the offset recorded on the entry being left and restored on traversal when the destination is Auto | `browser_session.cpp`, `main.cpp`, `AetherisBrowserSession` |
| Mixed content | done, 2026-11-23 — a secure document may not fetch insecure blockable content; images are the optionally-blockable category this engine renders. Trustworthiness is https, wss, file and loopback, per upstream's secure-contexts rule | `security_policy.cpp`, `secure_resource_loader.cpp`, `AetherisSecurityPolicy` |
| Flexbox: `flex` shorthand, basis, grow, shrink, `justify-content`, `align-items` | done, 2026-11-23 — `auto` basis has no intrinsic-width pass, so an item with neither basis nor width starts at 0 and grows | `layout.cpp`, `style.cpp`, `AetherisLayout` |
| Grid: tracks, areas, explicit placement | done, 2026-11-23 — `grid-template-columns`/`rows` tracks, `grid-template-areas` + `grid-area`, `grid-column`/`grid-row` with `start / end`, and auto-placement into the first free cell. Not read: named lines, `span n`, `auto-fit`/`minmax`, `grid-auto-flow: column` | `layout.cpp`, `AetherisLayout` |
| `transform` (translate/scale) | done | paint pass |
| `position: sticky` | done, 2026-11-23 — shifted at paint time, clamped to the containing block; `top` and `bottom` both read, `top` wins when they conflict | `render_tree.cpp`, `paint_executor.cpp` |
| `visibility` | done, 2026-11-18 — inherited, per-box, `collapse` as `hidden` | `RenderTree::emit()` |

## Layout

| Feature | State | Evidence |
| --- | --- | --- |
| Block flow | done | `layout.cpp` |
| Inline flow, line breaking, `white-space` | done — breaks at whitespace and at the cluster offsets the shaper allows; overflows when there is no opportunity, as a browser does (measured 2026-11-23) | `text_layout.cpp`, `AetherisTextLayout` |
| Flex | partial — no `flex-grow` at load, no `flex-shrink`/`basis` | `layout_flex_children()` |
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

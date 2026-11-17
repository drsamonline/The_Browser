# Aetheris build phases

**Author:** Dr. Sohil Momin · Part of the [Aetheris documentation set](USER_GUIDE.md)

The execution plan for the remaining work in [ROADMAP.md](ROADMAP.md), ordered by
[the audit](AUDIT.md). **Cheap and certain first; small items combined into one
batch; large tasks last.**

## Rules

1. **Combine by shared code path.** Items that touch the same file or subsystem
   ship in one batch, so they cost one build and one test cycle instead of
   several. The batch sizes below are chosen around that, not around features.
2. **Cheap and certain first.** No large task starts while a small one is open.
3. **Each batch ships on its own** — pushed independently, so a stall in one
   blocks nothing else.
4. **No batch is done without end-to-end evidence.** Unit tests alone do not
   count; the behaviour must be exercised through the real pipeline.
5. **Large tasks last** because they need the surface around them stable.

Effort: **S** ≈ one session · **M** ≈ a few sessions · **L** ≈ multi-session.

---

## Reviewed order, 2026-11-17

A review of this plan against the code found one false claim, one understated blocker and one stale line
(`ROADMAP.md` corrected in `e10caf02`). The order below supersedes the batch list underneath it; the batches
are kept for their detail, not for their sequencing. **Live progress is in
[CHECKLIST.md](CHECKLIST.md) — update it in the same commit as the work.**

### What the review changed

| Was | Now | Why |
| --- | --- | --- |
| Fonts: "wire the rasteriser into text layout" | Font **API** first: glyph ids and per-glyph advances | Layout measures `FontMetrics::advance(char)`, one byte at a time. No rasteriser can be wired into a layout that never asks for a glyph |
| `::before`/`::after` before custom properties | Custom properties first | Parser-level substitution vs a new node kind in `LayoutTreeBuilder`; far more pages per line |
| CSP listed as absent | Finish the existing path | The `<meta>` policy is read and inline scripts are gated already |
| Process/site isolation on the list | Out of scope | Upstream does not ship it in this shape; keeping it made every estimate wrong |

### Order

| Tier | Item | Upstream entry point | Proof it works |
| --- | --- | --- | --- |
| 0 | Coverage corpus — order the gaps by pages affected | — | a table of parse/cascade/layout/JS failures per site |
| 1a | Custom properties, `var()` | `Rust/src/css/custom_properties.rs` | reftest: `:root` var → element |
| 1b | Sibling combinators `+` `~` + sibling invalidation | `PreviousSiblings`, `AncestorTraversal` | reftest + a stale-frame control |
| 1c | `[a=v i]` | attribute matcher | reftest |
| 1d | `:checked`, `:disabled`, `:placeholder-shown` | form-state facts | reftest |
| 1e | `localStorage` | `HTML/Storage.*` | write, reload, read |
| 1f | `focus()`, `getBoundingClientRect` | `Element.idl` | the page asserts its own numbers |
| 2a | Font API: glyph ids, per-glyph advances | `LibGfx` font stack | line breaks change |
| 2b | Wire the rasteriser into paint | `glyph_rasterizer.cpp` | pixel: antialiased edge |
| 2c | Kerning | `LibGfx/FontCascadeList` | fixture per pair |
| 2d | Shaping, Arabic and Indic last | — | fixture per script |
| 3a | `::before` / `::after` synthetic boxes | `DOM/Element.cpp` pseudo-element generation | reftest: badge page |
| 3b | `position: sticky` | `Layout/Node.cpp` | scroll + pixel |
| 3c | Complete flex and grid | — | fixture per property |
| 4 | Shell UI over the existing `window_manager` | — | tabs switch, history navigates |
| 5a | CSP fetch directives | `ContentSecurityPolicy` | blocked-fetch fixture |
| 5b | Same-origin enforcement in the bindings | — | cross-origin fetch refused |
| 5c | Mixed-content blocking | — | http subresource on an https page refused |

### Batching

| Batch | Items | Shared code path |
| --- | --- | --- |
| A | 1b, 1c, 1d | CSS parser → `matches_simple` → resolver. One build, one test cycle, three features |
| B | 1e, 1f | JS bindings |
| C | 2a, 2b | `font.hpp` → `text_layout.cpp` → paint. **Do not split across sessions** |
| D | 5a, 5b, 5c | fetch path |

**Blocked here:** tier 0 needs network access this sandbox does not have (allowlisted hosts only). Tier 3c
needs layout fixtures that do not exist yet. `docs/COVERAGE.md` carries the corpus commands.

## Batch 1 — Bookkeeping and JS API polish (S)

Five small items, all in `src/js/js.cpp`, `src/main.cpp` and the docs. One cycle.

| Item | What |
| --- | --- |
| Version | Bump to **0.3.0** in `--version`, the banner, and a `v0.3` CHANGELOG section covering QuickJS, gumbo, events, timers and the DOM surface (audit P1) |
| `removeEventListener` | Add it, and de-duplicate identical `(type, listener, capture)` registrations so listener arrays cannot grow without bound (audit P2) |
| `dataset` | Make it writable — a setter writing `data-*` attributes — instead of a silent read-only snapshot (audit P2) |
| `children` | Cache the array and invalidate on mutation, so a naive `for` loop is not O(n²) (audit P2) |
| Docs | Fix the stale "39 regression test executables" in `USER_GUIDE.md` (audit P4) |

## Batch 2 — Put the security policy on the real fetch path (M)

Route the binary's fetching through `SecureResourceLoader` / `HttpResourceBackend`
instead of calling `HttpClient` directly in `src/main.cpp`, so the same-origin,
mixed-content and content-type policy that the tests exercise is actually
enforced on a real page (audit P3).

**Why this early:** a security policy that is tested but not enforced is worse
than none, because it reads as protection that is not there.

## Batch 3 — Forms (M)

`input`/`select`/`textarea` `value` and `checked`, `submit` events, and
`preventDefault` on submission. Builds on the event model already shipped.

## Batch 4 — CSS cascade breadth (M)

Media queries and pseudo-elements (`::before`/`::after`) together: both are
selector/cascade work in the same files, so they share one verification cycle.

## Batch 5 — Layout breadth (M)

`transform` (translate/scale/rotate) and `position: sticky` together: both are
layout and paint changes, separate from the cascade work in Batch 4.

## Batch 6 — Real-time scheduling (S)

`requestAnimationFrame` and `performance.now()`, plus an optional real-clock mode
for timers. Small, because the virtual-time loop already exists.

## Batch 7 — Certificate validation (M)

Hostname matching, chain checking and expiry in the TLS path.

## Batch 8 — Web security (M)

CSP, mixed-content blocking and same-origin enforcement in the JS bindings.
Depends on Batches 1 and 2.

## Batch 9 — Vector fonts (L, three sub-batches)

The biggest visual win and the biggest single task, so it comes after everything
smaller.

| Sub-batch | What |
| --- | --- |
| 9a | TrueType/OpenType parsing: `cmap`, `head`, `hhea`, `hmtx`, `loca`, `glyf` |
| 9b | Rasteriser, integrated into the text layout path (replaces the bitmap font) |
| 9c | Kerning (`kern`/`GPOS`) and a font fallback chain |

## Batch 10 — HTTP/2 (L, may stay deferred)

Large, and lower value than the batches above while the engine is a renderer.

## Batch 11 — UI shell (L, multi-batch)

Window, tabs, address bar, bookmarks and history. Turns the renderer into an
application.

## Deferred (not scheduled)

- Accessibility tree
- Developer tools / inspector
- Compositor-driven rendering
- Full-tree LibJS/LibWeb build (needs Rust, ICU, a clang plugin — a dependency
  decision, not a task)

## Verification recipe (every batch)

1. `cmake --build` the standalone profile.
2. `ctest --no-tests=error` — must stay green (51 tests today; new behaviour
   adds tests).
3. One end-to-end check through the real pipeline: a loopback page for DOM,
   networking and JS work; a rendered image for layout and paint work.
4. Compile the touched files under the project's `-Werror` set — CI fails on
   warnings, and this environment does not by default.
5. Update `CHANGELOG.md`, and `docs/ROADMAP.md` / `docs/AUDIT.md` if a finding
   closed.
6. Push the batch on its own.

---

*Aetheris Browser — build phases. Authored and maintained by **Dr. Sohil Momin**.*

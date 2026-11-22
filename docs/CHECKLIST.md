# Checklist — live progress

**Update this file in the same commit as the work.** A row is the unit; a commit either moves a row or it
does not belong to the plan.

## How to update

| Step | Rule |
| --- | --- |
| Tick a row | Only in the commit that makes it true. Never in a later "docs" commit on its own |
| Proof column | A commit sha, a test name, or a number from the window summary. **No proof, no tick** |
| Blocked row | Say what blocks it and where the unblocking work lives. Do not leave it looking merely unstarted |
| Out-of-scope row | `[-]`, with the reason. Never delete a row — a deleted row gets re-proposed |
| New work found mid-item | Add the row in the same commit, `[ ]`, at its tier |
| Status drift | If a row's status is wrong, fix it in the next commit and say so in the message |

Legend: `[ ]` not started · `[~]` in progress · `[x]` done · `[!]` blocked · `[-]` out of scope

## Tier 0 — order the work

| Row | Item | Status | Proof / blocker |
| --- | --- | --- | --- |
| 0.1 | Coverage corpus: render a fixed 20-site corpus, record what breaks | `[!]` | needs open internet; sandbox has allowlisted hosts only. Commands in `COVERAGE.md` |
| 0.2 | Publish the per-feature snapshot | `[x]` | `docs/COVERAGE.md`, `e10caf02` |

## Tier 1 — page compatibility, one code path per batch

| Row | Item | Status | Proof / blocker |
| --- | --- | --- | --- |
| 1a | Custom properties and `var()` | `[x]` | `a64cf097`; 6 cases in `TestAetherisWindowScripts` (`e6694100`) |
| 1b | Sibling combinators `+` `~`, with sibling invalidation | `[x]` | `dd310ca3`; 2 stripe pages + `uses_sibling_sequence()` |
| 1c | `[a=v i]` case flag | `[x]` | `dd310ca3`; both directions and the case-sensitive default |
| 1d | `:checked`, `:disabled`, `:placeholder-shown` | `[x]` | `dd310ca3`; 4 borderless controls, one per state |
| 1e | `localStorage` (six members) | `[x]` | `+` the commit before this one; one page asserts all six |
| 1e2 | `localStorage` across a page load | `[x]` | `+` the commit before this; two documents, one process |
| 1g | Navigation: `location.href`/`assign`/`reload`, a click follows a link | `[x]` | same commit; page B loads at /b |
| 1g2 | Realm isolation on navigation (old globals and listeners survive) | `[ ]` | deliberate limit of keeping the interpreter alive |
| 1g3 | `history.length`/`back()`/`forward()`, `location.replace()` | `[x]` | `+` the commit before this; replace keeps the length at 1 where a push would give 2 |
| 1g4 | `history.go(delta)` | `[x]` | `+` the commit before this; back/forward are now go(±1); go(0) reloads, asserted in the suite |
| 1g4b | `pushState`/`replaceState`, `history.state` | `[ ]` | not built — needs per-entry state, which means serialising a JS value |
| 1g4c | `scrollRestoration` | `[ ]` | not built |
| 1g5 | `location.origin`/`pathname`/`search`/`hash` (getters) | `[x]` | `+` the commit before this; four URL shapes |
| 1g6 | Dot-segment URL resolution | `[x]` | `+` the commit before this: `remove_dot_segments()` (nine unit-tested paths) wired into `resolve_url`; `/deep/dir/page` → `/deep/up` → `/up`, confirmed by the server log and the suite |
| 1g6-old | (superseded) **the resolver is correct and is not the cause.** With it wired, the server saw `/deep/dir/page` → `/deep/up` → `/up`, exactly right. The process hangs **inside `fetcher.fetch()` for the third document** — after the request goes out, before any content returns (`AETHERIS_TRACE_NAV=1`). Leading hypothesis: a keep-alive socket the server already closed, with no read timeout |
| 1g8 | **Fixed**: `Url::resolve` looped forever on a `..` at the root | `[x]` | `pos - 1` underflowed when the `..` was the first segment, `rfind` searched from the end, the replace was a no-op. `/../up` is now `/up`. Four unit cases; the fixture exits in under a second |
| 1g8-old | (superseded) **the fetch does not block**: `AETHERIS_TRACE_FETCH=1` shows both recvs completing in 2 ms and 0 ms, then nothing — no script output, no next frame. So the process stops inside document creation. Next instrument: print around `create_scripted_document` |
| 1g8-old | (superseded) **read, not yet fixed.** `timeout_ms` reaches `connect()` only — lines 524 and 531 of `include/aetheris/network/http_client.hpp` are its only uses — so nothing bounds `recv`. The three read loops (`read_headers`, `read_until`, `read_remaining`) all break on EOF and on error, so this is not a spin. What is measured: the third fetch blocks for more than 25 s with no further frame and no error. Next step: instrument inside the fetch — print before and after each `recv` with elapsed milliseconds — before changing anything |
| 1g7 | `location.pathname`/`search`/`hash` setters | `[x]` | `+` the commit before this; each navigates and carries its value, and an unchanged fragment makes no request |
| 1g7b | A fragment change does not re-fetch the document | `[x]` | `760b882c`; one request, script runs once, scroll to the indicated part (266 px, clamped from 700) |
| 1g7c | `location.hash` reads back synchronously | `[x]` | `d25f3343`; it always did. The claim that it did not came from a probe against a binary 73 minutes old |
| 4d | A timed-out build reads as a success | `[ ]` | **hazard**: `cmake --build \| grep error:` prints nothing when the build is killed at the 300 s limit. Check the binary's mtime, not the absence of errors |
| 4b | Split the window test | `[x]` | `60dfd68b`; four tests, two at a time: Core 47 s, Style 60 s, plus Layout and Geometry. Suite is 64 tests in 151-157 s |
| 4c | The split's "hidden dependency" | `[x]` | **not hidden state — 2 cores.** `-j4` on this box ran eight processes on two cores; the failures were contention. `ctest -j2` is green |
| 1f4 | `focus()` refuses a non-focusable target | `[x]` | `+` the commit before this; 10 cases measured, 7 in the suite |
| 1f5 | Rendering requirement: `display:none` is not focusable | `[x]` | `+` the commit before this; 3 cases incl. focus() at load |
| 1f7 | `visibility` — inherited, per-box paint, focusability | `[x]` | `+` the commit before this; 4 cases |
| 1f6 | A click focuses the nearest focusable ancestor, or nothing | `[x]` | `+` the commit before this; 6 cases |
| 1h | `:has(+ .x)`, `:has(~ .x)` | `[x]` | `+` the commit before this; 3 cases + the mutation |
| 1e3 | `sessionStorage` | `[x]` | `+` the commit before this; separate store, and each cannot see the other's keys |
| 1f | `getBoundingClientRect()` | `[x]` | `+` the commit before this one; page asserts its own numbers |
| 1f2 | `focus()`, `blur()` — state | `[x]` | `+` the commit before this one; two runs, pixels |
| 1f3 | `focus`/`blur` **events** reach `addEventListener` | `[x]` | root cause: one request slot, overwritten inside a single `run_timers()` window. Queue now |

## Tier 2 — fonts and text (the largest visual gap)

| Row | Item | Status | Proof / blocker |
| --- | --- | --- | --- |
| 2a | Font API: glyph ids and per-glyph advances | `[x]` | **was already there** — `advance()` asked the TTF; the gap was byte iteration, fixed `+` the commit before this |
| 2a2 | UTF-8 decoding in measure, measure_text and draw | `[x]` | `utf8_next()`; 5 decode cases + 2 measurement assertions in `AetherisTextLayout` |
| 2b | Wire `glyph_rasterizer` into paint | `[ ]` | — |
| 2c | Kerning | `[ ]` | — |
| 2d | Text shaping | `[x]` | `15c6496e`; `shape_run()` returns glyphs, clusters and advances from the font tables, and `AetherisShaping` pins the contract (fi -> one glyph, clusters as byte offsets) |
| 2d2 | Use the shaper in the text path | `[x]` | `6d8ca43c`; `measure` and the drawing pen both use shaped advances, `measure("fi")` 10.0938 -> 10.0781 |
| 2d3 | Cache the face per font | `[x]` | `perf` commit; keyed by a hash of the font data, not the buffer address. **Corrected 2026-11-22**: the cache was right, but taking that hash on every call cost 1.2331 ms of a 1.2571 ms run at -O2. The 0.075 ms in this cell was the face creation the cache removed, not the hash it added. Hash moved to load time in 2d4 |
| 2d4 | Shaping cost, measured in a release build | `[x]` | at -O2 a 24-character run cost **1.2571 ms**: HarfBuzz itself 0.0046 ms, the font-file hash the face cache took on every call **1.2331 ms (98%)**. Hash moved to load time (`TtfFont::identity()`, once per load) and shaped runs cached by (font identity, size, text): **0.0001 ms cached, 0.0086 ms cold** at -O2; 0.0027 / 0.1863 at -O0. `tools/run-shaping-bench.sh`; `AetherisShaping` pins the key |
| 2e | Bidi: split runs by direction before shaping | `[x]` | 2026-11-22; `bidi.cpp` classifies each code point, `split_directional_runs()` cuts at the direction change, and `shape_run()` shapes one buffer per run with the direction forced. "abc سلام xyz" guessed LTR: 12 glyphs, Arabic in isolated forms. Split: 11 glyphs, clusters descending inside the RTL run. `AetherisBidi` pins nine split cases, the cluster order and the gate |
| 2e2 | Reorder a line's runs into visual order (UAX#9 L2) | `[ ]` | `TextLayout::layout` emits one fragment per word, so an RTL line's words are painted left to right in *logical* order: "سلام عليكم" renders with سلام on the left (measured ink groups: 30 px left, 37 px right). Inside a fragment the direction is right as of 2e. Needs a paragraph direction and level resolution across the line's fragments |
| 2f | Line breaking by cluster (`HB_GLYPH_FLAG_UNSAFE_TO_BREAK`) | `[ ]` | the break pass uses whitespace, so a break can land inside a cluster. Was a "known gap, not a row" until 2026-11-22 |
| 2g | Widen the shaped-run key before adding letter-spacing | `[ ]` | the key is (font identity, size, text) because the engine has no letter- or word-spacing; upstream carries both in `ShapingCacheKey` as bit patterns. Add the bits with the property, not after it |

## Tier 3 — layout completeness

| Row | Item | Status | Proof / blocker |
| --- | --- | --- | --- |
| 3a1 | `::before`/`::after` computed style | `[x]` | `57d3a8eb`; five assertions in `AetherisCss`, including inheritance from the element rather than its parent |
| 3a2 | Generate the box when `content` is neither `normal` nor `none` | `[x]` | `51d7b016`; the box appears in the tree and `::before` paints its text |
| 3a5 | `::after` shares the line with `::before` | `[x]` | `e62c9529`; it painted all along, one line lower, because generated boxes took the block path. Both now content-sized on one line, asserted by pixel order |
| 3a2-old | (superseded) **attempted and reverted.** The box is created (the page reports one more layout node per `::before`/`::after`) but its text does not paint: a 24 px magenta `::before` on a white paragraph leaves the pixel white. Next step is to find which of the two paths drops it — `layout_inline_children`'s text branch requires `dom_node`, and `RenderTree::emit` may too — before writing the box again |
| 3a4 | `content: ''` generates an empty box | `[x]` | `f8ddd0f9`; magenta box at (20,55) against white at (20,95) for `content: none`. Needed a real `is_generated` flag: `generated_text.empty()` is false for exactly this case |
| 3a6 | `content` as a real list type, with alt text (`content: "x" / "alt"`) | `[ ]` | upstream holds a `StyleValueList` plus an optional alt-text list; the quoting in the parser stands in for the item type until this exists |
| 3a3 | Paint the generated box | `[ ]` | needs a synthetic text node whose `dom_node` is null, which several paths dereference |
| 3b | `position: sticky` | `[ ]` | — |
| 3c | Complete flex and grid (`flex-shrink`, `basis`, grid areas) | `[ ]` | — |
| 3e | Inline siblings flow onto one line | `[x]` | `+` the commit before this; three spans at x 0/19.6/39.9 on one line. Cause: the text run's box was given the whole line width |
| 3g | Inline wrapping at the container edge | `[x]` | `+` the commit before this; 8 spans in 100px → 5 + 3, two distinct y values |
| 3h | Soft-wrap opportunities | `[x]` | `+` the commits before this; two fixtures one space apart. Needed both the text-branch flag and the whole-line measurement |
| 3i | Overflow instead of wrapping when no opportunity exists | `[ ]` | **divergence**: at an exactly-full line this wraps anyway; upstream overflows. Ours never overflows — the safer error, recorded not hidden |
| 3h-old | (superseded) **three attempts, all measured, all reverted.** Upstream's rule read: a non-whitespace item breaks only when `line_ends_in_whitespace` or `can_break_before` (`inline_formatting_context.rs`, `text_item_wrap_opportunity`). Established: the whitespace text node between two inline elements **does** exist in this DOM (`html_tree_builder.cpp` inserts any non-empty text token), and the flow path's shrink-to-fit **clamps a child to the space left**, so the fit test needs the child's natural width measured against the whole line. What is still unexplained: with both of those fixed, a spaced pair still did not break, and the enclosing block's height changed. Next session starts from those two facts, not from the rule |
| 3f | Phrasing content is inline in the UA sheet | `[x]` | `+` the commit before this; spec-correct, but 3e shows it is not sufficient |
| 3d | `@font-face`, transitions, animations | `[ ]` | — |

## Tier 4 — shell

| Row | Item | Status | Proof / blocker |
| --- | --- | --- | --- |
| 4.1 | Tab strip over `window_manager` | `[ ]` | — |
| 4.2 | History and bookmarks | `[ ]` | — |

## Tier 5 — safety

| Row | Item | Status | Proof / blocker |
| --- | --- | --- | --- |
| 5a | CSP fetch directives (`img-src`, `connect-src`, `style-src`, …) | `[ ]` | meta policy and inline-script gating already exist |
| 5b | Same-origin enforcement in the bindings | `[ ]` | — |
| 5c | Mixed-content blocking | `[ ]` | — |
| 5d | Process and site isolation | `[-]` | out of scope, `e10caf02` |

## Closed — 2026-11-17, second session

Recorded so the same ground is not re-covered. Detail in `SESSION-HANDOFF-2026-11-17.md`, items 1–21.

| Area | What closed | Proof |
| --- | --- | --- |
| Layout | document-wide skip → per-node reuse on `(x, y, width)` → key comparison over restyled nodes only | 405 → 4–5 nodes; key checks 406 → 5 |
| Style | cache survives state changes; per-element `:focus-within` analysis; structural change invalidates its child sequence | 407 → 6 resolves; 408 → 4 |
| DOM | one attribute choke point replacing 6 silent writers; `classList`, `className`, `dataset`, `checked`, `value` made real | each 0 relayouts → 1, correct pixel |
| Click | relayout only when the click mutated the DOM | 407 resolves + 405 nodes → none |
| Selectors | `:empty`, `:root`, `-of-type`, `:nth-last-*`, `:not(list)`, `:is`, `:where`, `:has`, An+B | 5 pixel tables, every row asserted |
| CSSOM | `cssText`, `getPropertyValue`, `removeProperty`, `length`, `item()` | read back by the page |
| Verification | reftest matrix (11 shapes); honest `layout nodes:`; 3 false-green controls replaced | found a SIGSEGV and a missing `:empty` |
| Docs | `ROADMAP.md` claims corrected; `COVERAGE.md` added; `START-PROMPT.md` rewritten | `e10caf02`, `235f8956` |

## Closed — 2026-11-18

| Row | What | Proof |
| --- | --- | --- |
| 1b | `+` and `~`, with sibling invalidation on both a state change and a child-list change | `dd310ca3`, `43b7ff4a` |
| 1c | `[a=v i]`, which also found the fourth nested-syntax scanner (brackets) | `dd310ca3` |
| 1d | `:checked`, `:disabled`, `:placeholder-shown` | `dd310ca3` |

## Open defects

| Defect | State | Where |
| --- | --- | --- |
| 4th nested-syntax bug — brackets as well as parentheses | fixed in 4 scanners | `dd310ca3` |
| `specificity()` was the last scanner without a depth notion | **checked, correct**: brackets are consumed in one gulp and parens by the `:is`/`:not` branch | probe, 2026-11-18 |
| `specificity()` with nested parens (`:is(:not(.a))`) | mis-scores: it takes the first `)` | not fixed, rare |
| Upstream citations written from memory | two found 2026-11-18: `focus()` cited `DOM/Element.cpp` (no such method) and `location` was written before `HTML/Location.idl` was read | corrected; the rule is cite what you read |
| `is_focusable` overrides read after the base rule | reading `Element::is_focusable()` alone gives tabindex-only, which would have made every input unfocusable | caught before writing by reading `HTMLInputElement`/`HTMLAnchorElement` |
| `of S` argument in `:nth-child(2n of .x)` | not built | `style.cpp` |
| `register_subtree`/`purge_subtree` callers must note the child-list change | sharp edge | `main.cpp` |
| `DocumentRuntime::set_attribute` keeps its own `mark_dirty` contract | unaudited | `document_runtime.cpp` |


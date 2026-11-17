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
| 1e | `localStorage` / `sessionStorage` | `[ ]` | — |
| 1f | `getBoundingClientRect()` | `[x]` | `+` the commit before this one; page asserts its own numbers |
| 1f2 | `focus()`, `blur()` — state | `[x]` | `+` the commit before this one; two runs, pixels |
| 1f3 | `focus`/`blur` **events** reach `addEventListener` | `[ ]` | state moves, listener never runs. `set_focus()` guards on `dom.handle_by_node` |

## Tier 2 — fonts and text (the largest visual gap)

| Row | Item | Status | Proof / blocker |
| --- | --- | --- | --- |
| 2a | Font API: glyph ids and per-glyph advances; `measure_text` uses them | `[ ]` | — |
| 2b | Wire `glyph_rasterizer` into paint | `[ ]` | — |
| 2c | Kerning | `[ ]` | — |
| 2d | Shaping — Arabic and Indic last | `[ ]` | — |

## Tier 3 — layout completeness

| Row | Item | Status | Proof / blocker |
| --- | --- | --- | --- |
| 3a | `::before` / `::after` synthetic boxes | `[ ]` | — |
| 3b | `position: sticky` | `[ ]` | — |
| 3c | Complete flex and grid (`flex-shrink`, `basis`, grid areas) | `[ ]` | — |
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
| `of S` argument in `:nth-child(2n of .x)` | not built | `style.cpp` |
| `register_subtree`/`purge_subtree` callers must note the child-list change | sharp edge | `main.cpp` |
| `DocumentRuntime::set_attribute` keeps its own `mark_dirty` contract | unaudited | `document_runtime.cpp` |
| `element.focus()` moves the state but fires no `focus` event | open, measured 2026-11-18 | `set_focus()` in `main.cpp` |

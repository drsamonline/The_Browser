# Session handoff — 2026-11-18

Continues `SESSION-HANDOFF-2026-11-17.md` (items 1–21). This file covers the second session: batch A and B of
the reviewed plan, plus the five focus rows that came out of reading upstream.

**Read with**: `CHECKLIST.md` (live state), `HANDOVER.md` (mechanisms and gotchas), `PHASES.md` (order),
`COVERAGE.md` (what exists). This file is the narrative; those are the state.

## What shipped, in order

| # | Item | Commits | Measured |
| 22 | Sibling combinators `+` `~`, `[a=v i]`, `:checked`/`:disabled`/`:placeholder-shown` | `dd310ca3`, `43b7ff4a` | 2 stripe pages, 4 controls, both flag directions |
| 23 | `getBoundingClientRect()` | `f654adbf`, `8e90f626` | the page asserts its own numbers: `x 0, y 0, 200×50`, `right 200, bottom 50` |
| 24 | `element.focus()` / `blur()` state | `c1ec9b62`, `b73427e6` | two controls, focus then blur |
| 25 | focus/blur **events**, and the ordering bug behind them | `cffba72c`, `2323679e` | F,B in order; F,B,F ends focused |
| 26 | `localStorage` (six members) | `e142b937`, `5dbd9d37` | length 2→1→0, missing key `null`, `key(9)` `null` |
| 27 | Navigation: `location`, a click follows a link | `0f506632`, `58eff7ed` | page B loads at `/b`, reads what A stored |
| 28 | `:has(+ .x)`, `:has(~ .x)` | `9a912b7d`, `061eda79` | 3 load cases + a mutation that restyles backwards |
| 29 | `focus()` refuses a non-focusable target | `43338a63`, `1c5b95c2` | 10 cases, 7 in the suite |
| 30 | `display:none` is not focusable | `c7c00f02`, `db90789b` | 3 cases incl. `focus()` at load |
| 31 | A click focuses the nearest focusable ancestor | `919b2874`, `17e09604` | 6 cases |
| 32 | `visibility` — inherited, per-box paint, focusability | `d5662307`, `859f31b2` | 4 cases |

Suite 59/59 twice after every item. Tip at the end of the session: `7ce8034d`.

## The five things reading upstream changed

Every focus row came from comparing our code to `HTML/Focus.cpp`, not from a bug report. None of them would have
been caught by the suite, because the suite encoded what the engine did.

| Row | Upstream | Ours was |
| --- | --- | --- |
| 1f4 | `run_focusing_steps` maps a non-focusable target through `get_focusable_area()` and returns | focusing whatever it was handed |
| 1f5 | also requires connected, not `display:none`, `visibility:visible` | no rendering check at all |
| 1f6 | `focus_candidate_for_position()` walks up to the nearest focusable ancestor, null fallback | focusing the hit node |
| 1f7 | `visibility` inherited, checked per box in paint | property absent |
| 1f3 | — | a single request slot, overwritten inside one `run_timers()` window |

## Errors made, so the next session does not repeat them

| Error | Cost | Rule it earns |
| --- | --- | --- |
| Cited `DOM/Element.cpp` for the focusing steps without reading it | one wrong comment, caught when asked | a citation is a record of a read |
| `:has(> .x)` matched nothing: functional-pseudo capture stopped at a marker | 3 probes | a nested construct must not be scanned flat |
| `[data-mode=LIST i]` split on the space inside brackets | 1 probe | brackets are nested constructs too — the fourth scanner with this bug |
| Probe read the wrong input twice (UA border) | 3 measurements | say what the fixture puts at that coordinate |
| Probe asserted green for a `display:none` element | 1 test cycle | "not focusable" is "not magenta", not "green" |
| All eight `focus()` calls in one timer, only the last visible | 1 probe | one candidate per page |
| Click on a link used as a focus probe, after navigation shipped | 1 probe | ask what else the case triggers |
| `--click-x`/`--click-y` instead of `--click X,Y` | 2 silent probes | check the flag before the theory |

## Open, in the order the plan takes them

1. **Tier 2: the font API** — glyph ids and per-glyph advances, before any rasteriser wiring. Largest item on the
   plan; `FontMetrics::advance(char)` is the blocker.
2. Row 1g3: `history`, `location.replace()`, `location.pathname`/`search`/`hash`, dot-segment URL resolution.
3. Row 1g2: realm isolation on navigation — the interpreter surviving is what makes `localStorage` work, and also
   why the old page's globals and listeners survive.
4. Row 1e3 `sessionStorage`; row 3a `::before`/`::after`; row 3b `position: sticky`.
5. Tier 0 corpus — blocked here, no open internet.

## State at the end of the session

| Thing | Value |
| --- | --- |
| `main` tip | `7ce8034d` |
| Suite | 59/59, twice, ~103 s (`AetherisWindowScripts` is ~90 s of it) |
| Audit entry | `log/2026-11/2026-11-18-aetheris-batch-a-selectors.md`, 25624 bytes |
| Checklist | rows 1a–1h, 1f2–1f7 closed; 1e2/1e3/1g2/1g3 open |
| Sandbox | recycled three times during the session; the build recipe in `START-PROMPT.md` restored it each time |

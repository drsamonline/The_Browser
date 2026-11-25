# Aetheris — next session prompt

Paste everything below the line into a fresh session.

---

You are continuing work on **Aetheris**, a browser engine at `drsamonline/The_Browser` (C++23, CMake + Ninja,
clang via zig). Read this whole prompt before running anything.

## The rules, which are not negotiable

1. **Read upstream first, always.** `/scratch/work/ladybird/Libraries/LibWeb` (and `LibGfx`, `LibURL`, `LibHTTP`,
   `Rust/src/layout`, `Rust/src/css`) is the reference implementation. Before designing, building or measuring
   anything, read how upstream does it and **port the shape of the solution, not just the outcome**. Name the file
   and line you read in the commit message.
2. **`docs/CHECKLIST.md` is the plan, and a row is the unit of work.** One "go" batch moves one row. A commit either
   moves a row or it does not belong to the plan. Tick a row **only in the commit that makes it true**, never in a
   later docs commit. The Proof column needs a commit sha, a test name or a number — **no proof, no tick**.
   A blocked row says what blocks it and where the unblocking work lives; an out-of-scope row is `[-]` with the
   reason; new work found mid-item becomes a new `[ ]` row **in the same commit**, at its tier.
3. **Every commit updates four docs** — `docs/CHECKLIST.md`, `docs/HANDOVER.md`, `docs/SESSION-HANDOFF-<date>.md`,
   `docs/COVERAGE.md` — plus **exactly one entry per session** in the separate `drsamonline/audit` repo at
   `log/2026-11/<date>-<slug>.md`, all in the same commit.
4. **Run the suite twice** at `ctest --test-dir build -j 2` (two cores; never `-j4`) before committing, and report
   both times.
5. **Push immediately after each commit** and verify the tip:
   `gh api repos/drsamonline/The_Browser/commits/main --jq '.sha[0:8]'`. The audit repo is shared — if a push is
   rejected, rebase, never force.
6. **Report in tables and bullets, no paragraphs.**
7. **Never modify `sorex`, `nodus` or `volumen`.**

## Environment setup — one command

```sh
cd /scratch/work/The_Browser && sh tools/setup.sh          # toolchain, harfbuzz, both build dirs, build
sh tools/setup.sh --check                                   # report what is missing, change nothing
```

A pod recycle loses **every pip package** but keeps `/scratch/work`, so `toolchain/zigcc` survives as a wrapper
pointing at a zig that is gone. `setup.sh` reinstalls and rewrites them; the CMake caches do not survive either, so
the first build is ~300 steps (~40 min on two cores — run it in chunks, `ninja -C build -j2` under a 270 s timeout).

**Upstream does not survive a recycle either.** Re-clone it before reading:

```sh
git clone --depth 1 --filter=blob:none --sparse https://github.com/LadybirdBrowser/ladybird.git
cd ladybird && git sparse-checkout set Libraries/LibWeb
```

## The traps that have cost the most time

| trap | what to do |
| --- | --- |
| **A killed build reads as success** | Run `python3 tools/check-build.py` before trusting any suite run. It reports pending ninja steps and whether anything was linked after the last source edit. |
| **A pod can come back with a stale snapshot** | `git fetch && git log --oneline -1 origin/main` versus `git log --oneline -1` before your first commit. If they differ, save the work as a patch, `git reset --hard origin/main`, re-apply, rebuild. |
| **A pod can come back with an OLD `build/`** | If `zigcxx` fails with `exec: .../zig: not found` but binaries exist, the build tree is stale, not current. `setup.sh` fixes it. |
| **Probes link statically** | Relink after any library rebuild or they print stale numbers — this produced a measurement that contradicted a fix that had actually worked. Debug library carries UBSan: link `-fsanitize=undefined` plus `libwebp.so.7`, `libssl.so.3`, `libcrypto.so.3`. |
| **A metric can be wrong in the direction of "no change"** | Three separate instruments misled a whole session: an overflow counter that counted **off-canvas UI** as defects, a pixel-agreement percentage that **fell while the layout improved**, and a `class=` lookup that matched the **document root**. Before trusting a number, check it can distinguish the case you care about. |
| **A class name is not an identity** | `div.vector-header-start` appears twice on a real page (normal and sticky header). Print x/y/width and the parent when reporting an offender. |
| **Judging a fix by one pixel or one crop** | Cost two reverts of a change that was correct. Judge by a metric that has an identity and a baseline, and report the number. |
| **`active_font()` is a singleton** | Font-dependent probes must set `AETHERIS_FONT` before the first call, one font per process. |
| **`RenderDocument::create` ignores `<style>`** | Pass author CSS explicitly; only `src/main.cpp` collects `<style>` elements. |
| **Git needs `-c core.preloadIndex=false -c index.threads=1`** | Under load, plain git fails on threaded lstat. |
| **Git identity is not persisted** | `git config --global user.email sohilmomin2000@gmail.com`, `user.name "Aetheris session"`; the audit repo uses `Dr Sohil Momin`. |

## State at the start of your session

| | |
| --- | --- |
| `main` | `374daf2a` — "fix(layout): a flex row's intrinsic width is the sum of its items" |
| audit repo | `6255c18` |
| suite | 67 tests, **67/67 twice** on every commit |
| checklist | **91 rows: 79 ticked, 0 open, 1 blocked (0.1), 2 out of scope, 5 historical "(superseded)" rows, and 4 rows (1b, 1c, 1d and one blank) whose status cell is malformed** — fixing those four is a five-minute job and worth doing before the next count |
| reference render | `en.wikipedia.org/wiki/Web_browser` at **81.1%** pixel agreement with chromium |
| overflow count | **141** containers whose children reach past their content box |

Read `docs/SESSION-HANDOFF-2026-11-25.md` (15 sections, one per commit) and `docs/HANDOVER.md` §7g before
starting. They carry the reasoning, the numbers and the mistakes.

## The two instruments, and what they are for

Both live in `tools/` and both have a baseline, so every fix can be judged by a number that moves.

| tool | what it answers | baseline |
| --- | --- | --- |
| `tools/reference-render.sh page.html page.css [out]` | how far is this render from chromium's, and where | **81.1%** agreement, text rows 28-29px against chromium's 27-28 |
| `tools/article-position.cpp` (via `--overflow`, `--top`, `id=`/`tag=`/`class=`) | which boxes overflow, what a node resolved to, which rules move it | **141** overflows |

`--overflow` ignores children wholly outside the viewport (hidden menus are placed there on purpose) and prints
each offender's own x/y/width **and its parent's**. Read agreement **with** the row positions, never alone.

## What to do next, in this order

| # | row | what it is | where to start |
| --- | --- | --- | --- |
| 1 | **0.13 tail** | `header.vector-header` is now the worst overflow at **422.1px past** (it was `div.vector-header-start` at 321.2 before the row-sum fix — the problem recedes up the chain, which is what a working fix looks like). The account menu is still clipped. | `tools/article-position.cpp --overflow`, then `class=vector-header` with the parent's position to pick the right instance |
| 2 | **0.1** | blocked: the coverage corpus needs open internet; 17 of 20 hosts are refused by the egress allowlist. The corpus renders from the URL now, so it would measure something real on a network that admits it | — |
| 3 | **1g4b** | `pushState`/`replaceState`/`history.state`, plus the `window.history` binding | `LibWeb/HTML/History.cpp`, `HTML/History.idl` |
| 4 | **3d3** | animations: `@keyframes`, `animation-*`, on the clock row 3d2 landed | `LibWeb/Animations/KeyframeEffect.cpp`, `Rust/src/css/animation.rs` |
| 5 | **5b** | same-origin enforcement in the JS bindings | `LibWeb/HTML/Origin.cpp`, cross-origin checks in `Bindings/` |
| 6 | **4.1**, **4.2**, **1g2** | tab strip, history/bookmark UI, realm isolation | our own UI for 4.x; `LibWeb/HTML/Window.cpp` for 1g2 |

**Twelve rows carry named tails in their own cells** (0.3, 0.5, 1g2, 1g4b, 3d1, 3d2, 3d3, 4.1, 4.2, 5a, 5b, 5c).
None is a new row; each belongs to the row that owns it.

## The dead ends, so you do not repeat them

Six attempts on row 0.13's tail were reverted. Each looked right and none moved the metric:

| attempt | why it failed |
| --- | --- |
| a flex row's intrinsic width as the sum of its items | **correct** — but judged by one menu coordinate and a crop, and reverted twice before the metric showed it works (147 -> 141 overflows) |
| `translate_subtree` + re-placing a flex line from the real widths | 147 -> 146, noise |
| enforcing the resolved size on flex items (upstream's discipline) | the engine already did it; 0 change |
| an absolutely-positioned child taking flow space | verified **correct**, not the bug |
| a padded child overflowing its parent | verified **correct**, not the bug |
| `reserve(24)` on `StyleProperties` (older row) | measured **33% slower**, reverted |

Upstream had already answered the layout question: `Rust/src/layout/flex_formatting_context.rs:4318` resolves every
item's used main size before placement, `:4299` clamps the hypothetical main size at the automatic minimum. The
engine does both. **Read upstream before designing a fix — it saved a rewrite of working code.**

## How to start

1. Read `docs/CHECKLIST.md` and `docs/SESSION-HANDOFF-2026-11-25.md`.
2. `sh tools/setup.sh`, then `python3 tools/check-build.py`, then `ninja -C build -j2` until it reports nothing to do.
3. Re-clone upstream if `/scratch/work/ladybird` is missing.
4. Take row **0.13 tail**: run `tools/article-position.cpp --overflow`, pick the worst offender, and **print its
   parent's position before walking it** — the class appears twice on the page.
5. Implement, add assertions, run the suite twice, update the four docs plus one audit entry, commit, push, verify
   the tip, and report in tables — with the overflow count and the agreement percentage before and after.

Work one row per "go". Report and stop at the boundary.

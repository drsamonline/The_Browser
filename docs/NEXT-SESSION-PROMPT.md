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

**`setup.sh` does not fit in one 300 s call.** The two `cmake` configures take ~100 s and ~250 s on their own; run
each `cmake -S . -B <dir> ...` by hand first (the exact flags are in `tools/setup.sh`, lines 55-70), then build in
`ninja` chunks. A killed `setup.sh` looks like success — it writes its configure output to `/dev/null` and the
pipeline's exit code is `tail`'s.

**Upstream does not survive a recycle either.** Re-clone it before reading:

```sh
git clone --depth 1 --filter=blob:none --sparse https://github.com/LadybirdBrowser/ladybird.git
cd ladybird && git sparse-checkout set Libraries/LibWeb
```

**The probes and the reference page do not survive either.** `tools/article-position.cpp` is compiled by hand —
it is not a CMake target:

```sh
zigcxx -std=c++23 -O1 -g -fsanitize=undefined -DAETHERIS_RENDERING_FOUNDATION=1 -DAETHERIS_HAVE_HARFBUZZ=1 \
  -Isrc -Isrc/rendering -Ithird_party/harfbuzz/src tools/article-position.cpp -o /scratch/work/ap \
  build/lib/libAetherisRendering.a build/lib/libAetherisGumbo.a -Lbuild/lib -lharfbuzz \
  /usr/lib/x86_64-linux-gnu/libwebp.so.7 /usr/lib/x86_64-linux-gnu/libssl.so.3 \
  /usr/lib/x86_64-linux-gnu/libcrypto.so.3 -lm -lpthread
```

The reference page is two files: the HTML through `curl` (the proxy admits `en.wikipedia.org`), and its **two**
`<link rel=stylesheet>` URLs fetched and concatenated. 416,840 bytes of HTML and 215,770 bytes of CSS reproduce
the baselines below; any other size means you fetched a different page and the numbers are not comparable.

## The traps that have cost the most time

| trap | what to do |
| --- | --- |
| **A killed build reads as success** | Run `python3 tools/check-build.py` before trusting any suite run. It reports pending ninja steps and whether anything was linked after the last source edit. |
| **A pod can come back with a stale snapshot** | `git fetch && git log --oneline -1 origin/main` versus `git log --oneline -1` before your first commit. If they differ, save the work as a patch, `git reset --hard origin/main`, re-apply, rebuild. |
| **A pod can come back with an OLD `build/`** | If `zigcxx` fails with `exec: .../zig: not found` but binaries exist, the build tree is stale, not current. `setup.sh` fixes it. |
| **Probes link statically** | Relink after any library rebuild or they print stale numbers — this produced a measurement that contradicted a fix that had actually worked. Debug library carries UBSan: link `-fsanitize=undefined` plus `libwebp.so.7`, `libssl.so.3`, `libcrypto.so.3` by full path (`-lcrypto` finds nothing: there is no dev symlink). |
| **A metric can be wrong in the direction of "no change"** | Four instruments have now misled a session: an overflow counter that counted **off-canvas UI** as defects, a pixel-agreement percentage that **fell while the layout improved**, a `class=` lookup that matched the **document root**, and a page-sized agreement score that read **99.7%** on a fixture where two boxes were 40px from where chromium puts them. Before trusting a number, check it can distinguish the case you care about. |
| **A test that cannot fail** | Two assertions were written on 2026-11-26 and one of them **passed on the unfixed engine**, because the thing it measured was not measured either way. Revert the fix, rebuild, and watch the test go red. If it stays green, the test is not proof. |
| **A class name is not an identity** | `div.vector-header-start` appears twice on a real page (normal and sticky header). Print x/y/width and the parent when reporting an offender. |
| **Judging a fix by one pixel or one crop** | Cost two reverts of a change that was correct. Judge by a metric that has an identity and a baseline, and report the number. |
| **`active_font()` is a singleton** | Font-dependent probes must set `AETHERIS_FONT` before the first call, one font per process. |
| **`RenderDocument::create` ignores `<style>`** | Pass author CSS explicitly; only `src/main.cpp` collects `<style>` elements. |
| **Git needs `-c core.preloadIndex=false -c index.threads=1`** | Under load, plain git fails on threaded lstat. |
| **Git identity is not persisted** | `git config --global user.email sohilmomin2000@gmail.com`, `user.name "Aetheris session"`; the audit repo uses `Dr Sohil Momin`. |
| **The probe is slow on a real page** | `article-position` on the wikipedia page takes **~110 s** in the Debug library. One call per mode, never two in one shell command, and write the output to a file. |

## State at the start of your session

| | |
| --- | --- |
| `main` | `ba8404e1` — "fix(layout): a page's indentation is not its content (rows 3m and 3l)" |
| audit repo | `97d8517` |
| suite | 67 tests, **67/67 twice** on every commit |
| checklist | **96 rows: 81 ticked, 2 open (0.14, 3n), 1 blocked (0.1), 2 out of scope (3i, 5d), 5 historical "(superseded)" rows, and 3 duplicate rows in the `Closed — 2026-11-18` table** |
| reference render | `en.wikipedia.org/wiki/Web_browser` at **80.4%** pixel agreement with chromium |
| overflow count | **139** containers whose children reach past their content box |

Read `docs/SESSION-HANDOFF-2026-11-26.md` (10 sections) and `docs/HANDOVER.md` §7g and §7h before starting.
They carry the reasoning, the numbers and the mistakes.

## The two instruments, and what they are for

Both live in `tools/` and both have a baseline, so every fix can be judged by a number that moves.

| tool | what it answers | baseline |
| --- | --- | --- |
| `tools/reference-render.sh page.html page.css [out]` | how far is this render from chromium's, and where | **80.4%** agreement, text rows 28-29px against chromium's 27-28 |
| `tools/article-position.cpp` (via `--overflow`, `--top`, `id=`/`tag=`/`class=`) | which boxes overflow, what a node resolved to, which rules move it, and **both** intrinsic widths | **139** overflows |

`--overflow` ignores children wholly outside the viewport (hidden menus are placed there on purpose) and prints
each offender's own x/y/width **and its parent's**. Read agreement **with** the row positions, never alone.

`class=`/`id=` prints, for the node: its children, the rules that change it, `max=` and `min=` for every node to
depth 4, and the ancestor chain. **Both** intrinsic widths, because an item that overflows is either bigger than
its base (a layout bug) or floored above the space it was given (a sizing bug) — and the fix is different.

## What to do next, in this order

| # | row | what it is | where to start |
| --- | --- | --- | --- |
| 1 | **0.14** | The page title bar is painted **black** and placed ~410px below where chromium puts it. It is the worst band of the reference render by a factor of two (y 512-576 at 46.4%, y 576-640 at 22.9%) and it is what makes the whole-page percentage unreadable. | Crop both renders at y 500-700. Two separate questions: why black (row 0.8 fixed unparseable colours painting opaque black, so something here resolves to black rather than transparent) and why 410px low (a block-order question, not a paint one) |
| 2 | **3n** | A lone flex item is never shrunk to its automatic minimum: `item.base = std::min(available_width, max_content_width(child))` caps it at the line width, so free space is exactly zero and the floor never runs | `layout_flex_children`, the base-size loop |
| 3 | **0.1** | blocked: the coverage corpus needs open internet; 17 of 20 hosts are refused by the egress allowlist. The corpus renders from the URL now, so it would measure something real on a network that admits it | — |
| 4 | **1g4b**, **3d3**, **5b** | ticked with named tails in their own cells: structured clone, `animation-direction`/`fill-mode`/`play-state`, and the origin check that has nowhere to run until a `fetch` or an iframe exists | the cells say what is left |

**Twelve rows carry named tails in their own cells** (0.3, 0.5, 1g2, 1g4b, 3d1, 3d2, 3d3, 4.1, 4.2, 5a, 5b, 5c).
None is a new row; each belongs to the row that owns it.

## The dead ends, so you do not repeat them

Seven attempts on row 0.13 were reverted or rejected. Each looked right and none moved the metric:

| attempt | why it failed |
| --- | --- |
| a flex row's intrinsic width as the sum of its items | **correct** — but judged by one menu coordinate and a crop, and reverted twice before the metric showed it works (147 -> 141 overflows) |
| `translate_subtree` + re-placing a flex line from the real widths | 147 -> 146, noise |
| enforcing the resolved size on flex items (upstream's discipline) | the engine already did it; 0 change |
| an absolutely-positioned child taking flow space | verified **correct**, not the bug |
| a padded child overflowing its parent | verified **correct**, not the bug |
| `reserve(24)` on `StyleProperties` (older row) | measured **33% slower**, reverted |
| `"ab\tcd"` vs `"ab"` as a test of the minimum width | **passed on the unfixed engine** — a tab is not measured either way. Replaced, not kept |

Upstream had already answered the layout question: `Rust/src/layout/flex_formatting_context.rs:4318` resolves every
item's used main size before placement, `:4299` clamps the hypothetical main size at the automatic minimum, and
`:1217` generates no item at all for a run of white space. The engine now does all three.
**Read upstream before designing a fix — it saved a rewrite of working code, twice.**

## How to start

1. Read `docs/CHECKLIST.md` and `docs/SESSION-HANDOFF-2026-11-26.md`.
2. Configure and build (`tools/setup.sh` will not fit in one call — see above), then
   `python3 tools/check-build.py`, then `ninja -C build -j2` until it reports nothing to do.
3. Re-clone upstream if `/scratch/work/ladybird` is missing; rebuild the probe and the reference page if
   `/scratch/work/ap` and `/scratch/work/reference` are missing.
4. Take row **0.14**: crop both renders at y 500-700 first. The black bar hides everything else, so nothing
   else can be judged until it is explained.
5. Implement, add assertions **and prove each one fails without the fix**, run the suite twice, update the four
   docs plus one audit entry, commit, push, verify the tip, and report in tables — with the overflow count and the
   agreement percentage before and after.

Work one row per "go". Report and stop at the boundary.

**Two things this session learned the hard way.** A pod recycle can come back with an **empty `/scratch/work`**
— the repo, the build trees and the upstream clone are gone, and only a delivered patch survives; commit or
patch before you stop. And a whole-page agreement percentage is dominated by whichever single defect is biggest,
so it cannot judge a row that does not touch that defect — read the per-band table instead.

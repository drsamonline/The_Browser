# Aetheris — next session prompt

Paste everything below the line into a fresh session.

---

You are continuing work on **Aetheris**, a browser engine at `drsamonline/The_Browser` (C++23, CMake + Ninja,
clang via zig). Read this whole prompt before running anything. It was written on 2026-11-27 after four days of
layout work on `en.wikipedia.org/wiki/Web_browser`, and it is deliberately ordered so you spend your time on
engine work rather than on rediscovery.

## The rules, which are not negotiable

1. **Read upstream first, always.** `/scratch/work/ladybird/Libraries/LibWeb` (`Rust/src/layout`,
   `Rust/src/css`, `CSS/Properties.json`) is the reference. Name the file and line you read in the commit message.
2. **`docs/CHECKLIST.md` is the plan, and a row is the unit of work.** One "go" batch moves one row. Tick a row
   **only in the commit that makes it true**, with a sha, a test name or a number as proof — **no proof, no tick**.
   New work found mid-item becomes a new `[ ]` row in the same commit, at its tier.
3. **Every commit updates four docs** — `docs/CHECKLIST.md`, `docs/HANDOVER.md`,
   `docs/SESSION-HANDOFF-<date>.md`, `docs/COVERAGE.md` — plus **one entry** in the separate
   `drsamonline/audit` repo at `log/2026-11/<date>-<slug>.md`, all in the same commit.
4. **Run the suite twice** at `ctest --test-dir build -j 2` (two cores; never `-j4`) before committing, and
   report both times.
5. **Push immediately, verify the tip**: `gh api repos/drsamonline/The_Browser/commits/main --jq '.sha[0:8]'`.
   If a push is rejected, rebase — never force.
6. **Report in tables and bullets, no paragraphs.**
7. **Never modify `sorex`, `nodus` or `volumen`.**
8. **Do not hand the user files.** Work lands as commits. A patch file is only ever insurance when the pod
   cannot push, and it should be deleted once the commit is in.

## State at the start of your session

| | |
| --- | --- |
| `main` | `8dd1bafe` — "fix(layout): a percentage height is not a percentage of the width (row 0.15)" |
| branch | `wip/row-0.18-inline-padding` at `18e63235` — **the inline-padding fix, built but never suite-run** |
| audit repo | `91410e5f` |
| suite | 67 tests; last green run 67/67 twice on `8dd1bafe` |
| reference render | **84.1%** pixel agreement, engine text rows 15px below chromium's |
| overflow count | 139 |

Read `docs/SESSION-HANDOFF-2026-11-26.md` (§§ 1-15) before starting: it carries every number and every mistake.

## Do this first — it is 40 minutes and it unblocks everything

**Finish row 0.18, which is already written and pushed as a branch.**

```sh
cd /scratch/work && git clone https://github.com/drsamonline/The_Browser.git && cd The_Browser
git checkout wip/row-0.18-inline-padding
sh tools/setup.sh            # see "Environment" below: it will NOT fit in one call
python3 tools/check-build.py # must report every binary current
./build/bin/TestAetherisAdvancedLayout
```

Then prove the assertion can fail: revert the `layout.cpp` hunk, rebuild, watch it fail with *"an inline box's
vertical padding does not push the next line down"*, restore. Then `ctest --test-dir build -j 2` **twice**, tick
row 0.18 as `[x]`, add its audit entry, and merge to `main`.

What it fixes: `layout_inline_children` added an inline box's vertical padding to the line, so a page's table of
contents (`<a><div class="vector-toc-text" style="padding:6px 0">`) came out 28.8px per row instead of 16.8.
css2 §8.1 / §10.6.1: inline vertical padding paints outside the line box and does not move it. Fixture in the
test: two block `<a>` each holding a padded span are 16.8px apart, block 33.6 tall; before, 28.8 and 57.6.
**No page-level effect** (84.3% → 84.1%) — it is a compatibility fix, not a page moved.

## Then, in this order

| # | row | what it is | why it is ordered here |
| --- | --- | --- | --- |
| 1 | **0.17** | **`calc()` in a media query is silently false.** The evaluator cut the feature value at the first `px`, so `@media screen and (max-width: calc(1120px - 1px))` became `"calc(1120"` and never matched. Fixture: that query gave 300px, `max-width: 1119px` gave 50px. On the page it is why `#vector-toc-pinned-container` is **350px** instead of its `200px` cap, which pushes everything below it down. | The fix is written and works (`media_length()` in `css_parser.cpp`, `calc()` over px lengths): fixture 300 → **50**, TOC 350 → **200**. **It is NOT in the branch because enabling it regressed the page**: agreement 84.1% → **77.6%** and the spaces between text and links vanished — "often abbreviated asbrowser, is anapplication for accessingwebsites". Land it only together with the text bug it exposes, or land it and fix that immediately |
| 2 | **0.19** (open it) | **A space before a link disappears once the narrow-layout rules apply.** The paragraph reads "asbrowser", "anapplication", "accessingwebsites" — the trailing space of a text run is dropped when an `<a>` follows. | This is what makes 0.17 unshippable. Find it with a four-line fixture: `<p>a <a href="#">b</a> c</p>` under `@media (max-width: calc(1120px - 1px))`, and `layout_inline_children`'s `line_ends_in_whitespace` path |
| 3 | **3n** | A lone flex item is never shrunk to its automatic minimum: `item.base = std::min(available_width, max_content_width(child))` caps it at the line width, so free space is exactly 0 and the floor never runs | Small, self-contained, and it closes the last flex-sizing gap |
| 4 | **0.1** | blocked: the coverage corpus needs open internet; 17 of 20 hosts are refused by the egress allowlist | — |

**Row 0.17 has now had three wrong explanations** — "the TOC belongs in a column" (it does not; chromium stacks
it at 1024px too), "the article ends early" (it does not; the TOC was simply not capped), and finally the media
query. The lesson is in `HANDOVER.md` §7j: measure the *why* before writing it into a row, because the next
person builds on the explanation, not the number.

## Environment — what actually happens on a pod

| | |
| --- | --- |
| **`/scratch/work` sometimes survives a recycle and sometimes comes back empty.** It survived on 2026-11-27 and was empty on 2026-11-26. Never assume it; commit early. |
| **pip packages never survive.** `ziglang`, `cmake`, `ninja` must be reinstalled: `pip install -q ziglang cmake ninja`, then rewrite `/scratch/work/toolchain/zigcc` and `zigcxx` to `exec <ziglang>/zig cc|c++ "$@"`. |
| **A toolchain reinstall invalidates the CMake cache** even when `build/` survives — expect **~300 steps**, ~40 min at ~10-20 s each on two cores. Run `ninja -C build -j2` in chunks under 280 s and let it resume. |
| **`tools/setup.sh` does not fit in one call** (two `cmake` configures, ~100 s and ~270 s). Configure by hand, one call each, or run it and accept that the second configure may be killed. |
| **A pod can fill its process table.** 10-17k unreaped zombies under PID 1 against a hard `ulimit -u` of 4096 makes `zig`, `ninja`, `chromium` and `git push` fail with `Cannot fork` / `failed to spawn: SystemResources`. Nothing recovers it; a recycle is the fix. Save your work as a pushed branch *before* you hit it. |
| **Probes are not CMake targets.** `tools/article-position.cpp` must be compiled by hand, and **relinked after every library rebuild** or it prints stale numbers (this has produced a wrong measurement twice): see the command in `HANDOVER.md` §7g. |
| **The reference page does not survive.** Re-fetch `en.wikipedia.org/wiki/Web_browser` (416,840 bytes of HTML) and concatenate its **two** `<link rel=stylesheet>` URLs (215,770 bytes of CSS) — any other size means a different page and the numbers are not comparable. |

## The instruments, and what each can and cannot say

| tool | answers | baseline |
| --- | --- | --- |
| `tools/reference-render.sh page.html page.css [out]` | how far a render is from chromium's, and where | **84.1%**, engine text rows 15px below chromium's |
| `tools/article-position.cpp` (`--overflow`, `--overflow --all`, `--top`, `id=`, `tag=`, `class=`) | which boxes overflow, what a node resolved to, which rules move it, and **both** intrinsic widths | **139** overflows |
| a paint-command probe (`cmds`, written ad hoc on 2026-11-26) | which command paints a given y band, with its type, rect and colour | — it named the black title bar in one line |

**The whole-page percentage is dominated by whichever single defect is biggest, and it has moved the wrong way
three times while the layout improved** (90.3 → 76.9 on row 0.11; 88.8 → 84.3 on row 0.15). Always print the
per-band table and the row positions alongside it:

```python
# per-band agreement, 64px bands — the cheapest way to see WHERE a render differs
for y0 in range(0, 768, 64): ...
```

## The traps that have cost the most time

| trap | what to do |
| --- | --- |
| **A test that cannot fail** | Three assertions so far passed on the unfixed engine. Revert the fix, rebuild, watch it go red. If it stays green, the test is not proof. |
| **An exact float comparison** | `16.8f == 16.799999` fails. Compare with a tolerance. |
| **A stale probe** | Probes link the library statically. Relink after every rebuild. |
| **A wrong premise** | Three rows in a row were opened with the wrong *why*. Measure it before writing it down. |
| **A scaled screenshot** | Reading a resized composite gave a 17px row spacing where the render had 28px. Measure the PNG at 1:1 with a pixel scan, not with your eyes. |
| **`git status` fails under load** | `-c core.preloadIndex=false -c index.threads=1`. |
| **Git identity is not persisted** | `user.email sohilmomin2000@gmail.com`, `user.name "Aetheris session"`; the audit repo uses `Dr Sohil Momin`. |
| **A media query can be silently false** | Check that a rule you expect actually applied — a node that reports `display=block` where the CSS says `grid` is a selector or a media-query failure, not a layout failure. |

## How to start

1. Read `docs/CHECKLIST.md` and `docs/SESSION-HANDOFF-2026-11-26.md` §15.
2. Restore the environment (above), build in chunks, `python3 tools/check-build.py` until clean.
3. Finish row **0.18** from `wip/row-0.18-inline-padding` (§ "Do this first").
4. Then **0.17 + 0.19** together — the `calc()` media query and the space it loses — because either alone
   makes the render worse.
5. Report in tables, with the per-band numbers and the row positions, and stop at the boundary.

Work one row per "go".

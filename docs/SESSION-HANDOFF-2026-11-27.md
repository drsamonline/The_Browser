# Session handoff — 2026-11-27

One commit: **an inline box's vertical padding does not make the line taller.** Row 0.18 closes, and it is the
first row in this plan that was finished without moving a page-level number — correctly, because it is a
compatibility fix.

## 1. Where this session started

A fresh pod. `/scratch/work` was **empty**: no repo, no upstream, no toolchain, no `build/`. The process table was
clean (4 processes, 0 zombies against `ulimit -u` 4096), so nothing had to be recovered — only rebuilt.

| step | what | how long |
| --- | --- | --- |
| toolchain | `pip install -q ziglang cmake ninja`, then rewrite `toolchain/zigcc` and `zigcxx` | ~40 s |
| harfbuzz | `tools/fetch-harfbuzz.sh` inside `setup.sh --no-build`, 38,929 KB, **harfbuzz 15.0.0** | ~35 s |
| configure | `cmake -S . -B build -G Ninja …` **by hand**, not through `setup.sh` | **53.8 s** |
| build | 300 steps, `ninja -C build -j2`, **7 chunks** under 275 s | ~32 min |
| verify | `python3 tools/check-build.py` -> **all 68 binaries current** | — |

**`tools/setup.sh --no-build` does not fit in one call**, exactly as the prompt warned: it was killed at the
`build: configuring` step with no `CMakeCache.txt` written. Running the `cmake` command itself took 54 s, so the
cost is not the configure — it is `pip install` + harfbuzz fetch + configure in one process. Configure by hand.

**`main` was not at `8dd1bafe`.** Two docs-only commits had landed on top (`67b83450`, `51999344`), so the branch
point the prompt names is two behind the tip. `wip/row-0.18-inline-padding` was intact at `18e63235`.

## 2. The baseline had to be re-created, and the page has changed

| baseline | 2026-11-26 | 2026-11-27 | comparable? |
| --- | --- | --- | --- |
| `en.wikipedia.org/wiki/Web_browser` HTML | 416,840 B | **421,819 B** | **no** — the page has been edited |
| its two stylesheets | 215,770 B | **215,770 B** | yes, byte-identical |
| pixel agreement | 84.1% | **84.3%** without the fix, **84.1%** with it | only within this session |
| overflows | 139 | **139** | yes |

**The absolute number is not comparable across sessions any more, and that is a property of the instrument, not a
mistake.** What is comparable is a pair measured on the *same* HTML in the *same* session, which is what row 0.18's
proof is. The next session should re-fetch and re-measure rather than trust 84.1% as a constant; the 5 KB of extra
markup moved it by 0.2 points on its own.

Engine text rows, unchanged by this fix and identical in both renders:

```
11, 44, 150, 179, 207, 236, 265, 293, 306, 323
reference: 16, 43, 46, 135, 165, 193, 221, 247, 274, 305
```

## 3. Row 0.18 — an inline box's vertical padding made the line taller

css2 §8.1 and §10.6.1: the vertical padding, border and margin of an **inline, non-replaced** box start at the top
and bottom of its content area and have nothing to do with the line-height; **only the line-height is used when
calculating the height of the line box**. The padding paints outside the line and does not advance the next line.

`layout_inline_children` added `child->rect.height` — the whole border box, padding included — to the line's
`max_bottom`. A page's table of contents is `<a><div class="vector-toc-text" style="padding:6px 0">`, so every row
came out **28.8px** where a browser gives **16.8**, and the list pushed the article down.

The line now contributes `max(line_height, content height)` for an inline non-replaced child, and the full border
box for an **inline-block or a replaced element**, which are atomic and do move the line.

Upstream's shape, read for this commit: `Rust/src/layout/line_box.rs:214-217` builds the line's block length as the
max over fragments of the fragment's **content** block size plus its border-box block-start/end — per-fragment
quantities, never a child's whole border box — and `Rust/src/layout/line_builder.rs:671-676` passes
`box_cells.content_block_size` with the padding and border as separate edges.

### The fixture, measured both ways

Two block `<a>`, each holding a `<span style='padding: 6px 0'>`, `font-size: 14px; line-height: 1.2`:

| | unfixed | fixed | chromium |
| --- | --- | --- | --- |
| gap between the two blocks | **28.80** | **16.80** | 16.8 |
| block height | **57.60** | **33.60** | 33.6 |

### The assertion can fail, and was watched failing

`AetherisAdvancedLayout` asserts both halves. With `src/rendering/layout.cpp` reverted to `origin/main` and rebuilt,
it fails with exactly:

```
FAILED: an inline box's vertical padding does not push the next line down
```

Restored, it passes. Suite **67/67 twice** — 180.28 s and 152.45 s.

## 4. What this row did *not* do, and that is the correct outcome

| | before | after |
| --- | --- | --- |
| pixel agreement | 84.3% | **84.1%** |
| engine text rows | 11, 44, 150, … | **identical** |
| overflow count | 139 | **139** |

No band moved. A fix that makes the engine agree with a browser on a construct the page uses is still worth its
commit; it simply does not show up in a whole-page percentage, and the prompt's claim that it would not is
confirmed rather than assumed.

**Four rows now have moved agreement the wrong way while the layout got better** — 0.11 (90.3 -> 76.9), 0.15
(88.8 -> 84.3), 3m (81.2 -> 80.4) and this one (84.3 -> 84.1). Read the per-band table and the row positions, never
the number alone.

## 5. What the next session must do, in this order

1. **Row 0.17 + 0.19 together.** `media_matches` in `css_parser.cpp:210` cuts a media feature's value at the first
   `px` — `value.substr(0, value.find("px"))` — so `max-width: calc(1120px - 1px)` becomes `"calc(1120"`,
   `strtof` fails, and the query is **silently false**. The fix is a `calc()` evaluator over px lengths. It is
   **not** in this tree and not in any branch; it has to be written.
2. **Row 0.19 must ship with it.** Enabling the query regresses the page (84.1% -> 77.6% by the previous
   session's measurement) because it exposes a second bug: the trailing space of a text run is dropped when an
   `<a>` follows, giving "asbrowser", "anapplication", "accessingwebsites". Fixture: `<p>a <a href="#">b</a> c</p>`
   under `@media (max-width: calc(1120px - 1px))`; the code path is `layout_inline_children`'s
   `line_ends_in_whitespace`.
3. **Row 3n** — `item.base = std::min(available_width, max_content_width(child))` caps a lone flex item at the line
   width, so `free_space` is exactly 0 and the automatic-minimum floor never runs.
4. **Row 0.1** stays blocked: 17 of 20 corpus hosts are refused by the egress allowlist.

### One measurement for 0.17, taken here and not yet used

`tools/article-position.cpp id=vector-toc-pinned-container` on the 2026-11-27 page, **with** the 0.18 fix in place:

```
vector-toc-pinned-container  x=12.0  y=107.3  w=988.0  h=350.4  display=block  position=static
  its ul                     x=28.0  y=140.9  w=956.0  h=316.8
```

So the container is **988 wide and 350.4 tall** — the "350" in the plan is its **height**, not its width, and the
`ul` inside it is 316.8. Whoever opens 0.17 should measure the height against chromium's before writing the row's
explanation, because this row has already had three wrong ones.

## 6. Traps that cost time in this session

| trap | the detail |
| --- | --- |
| The probe would not link | `libAetherisRendering.a` is built with UBSan, so `article-position.cpp` needs `-fsanitize=undefined` or the link dies on `__ubsan_handle_type_mismatch_v1` |
| The probe would not run | it needs `LD_LIBRARY_PATH=build/lib` for `libharfbuzz.so` |
| `--overflow` is positional | `ap page.html page.css --overflow`, not `ap --overflow page.html page.css` — the flag first gives "cannot read --overflow" |
| The probe is slow | one `--overflow` run is ~90 s; two in one `bash` call hit the 200 s limit |
| A tool-call timeout mid-pipeline | the `cp` at the end of a timed-out call does **not** run, so the tree is left on the reverted `layout.cpp`. Check `git status` after any timeout |
| `setup.sh` in one call | killed mid-configure, leaving no `CMakeCache.txt`; configure by hand |

## 7. State at the end of this session

| | |
| --- | --- |
| `main` | row 0.18 merged (this commit) |
| suite | **67/67 twice** |
| pixel agreement | **84.1%** on the 2026-11-27 page (421,819 B HTML, 215,770 B CSS) |
| overflow count | **139** |
| engine text rows | 11, 44, 150, 179, 207, 236, 265, 293, 306, 323 |
| rows open in tier 0 | 0.17, 0.1 (blocked), 0.14 (`[~]`) |
| rows open in tier 3 | 3n |

---

# Second batch, same day — rows 0.17 and 0.19

Two commits of work in one batch, because either alone makes the render worse: the `calc()` media query exposes a
dropped space, and the space fix has nothing to fix until the query matches.

## 8. The pod, twice

The first pod died of the documented process-table trap mid-build: **11,252 zombies** against a hard `ulimit -u` of
4096, so `git push`, `ninja`, `timeout` and `subprocess` all failed for three consecutive turns. Both fixes were
written and verified as far as they could be without a compiler (arithmetic ported to Python, the patch checked with
a hand-written diff applier), and shipped as a patch file. **A recycled pod then took the build in one go** — the
stale `/scratch/work` came back with 155 object files and a `CMakeCache.txt` whose compilers still resolved, but
ninja rebuilt all 300 steps anyway, exactly as `HANDOVER.md` §7f says it will.

## 9. Row 0.17 — `calc()` in a media query

| | |
| --- | --- |
| cause | `media_matches` read `value.substr(0, value.find("px"))`, so `calc(1120px - 1px)` became `"calc(1120"` |
| second bug | the feature's close paren was `rfind(')')`, the last one in the string |
| fix | `LengthExpression`, a recursive-descent evaluator; `parse_media_length()` rejects `min()`/`max()`/`clamp()` |
| upstream | `Rust/src/css/parser/query_parser.rs:2111-2131` |

**Scope, measured** by porting both evaluators to Python and running them over the real `page.css`:

| | |
| --- | --- |
| `@media` blocks | 76 |
| distinct preludes | 17 |
| blocks that change state | **13** |
| rules inside them | **29** |
| of which `calc()` preludes | **13 of 13** |

| old | new | blocks | rules | prelude |
| --- | --- | --- | --- | --- |
| false | **true** | 10 | 20 | `screen and (max-width:calc(1120px - 1px))` |
| false | **true** | 2 | 2 | `screen and (min-width:calc(640px - 1px))` |
| false | **true** | 1 | 7 | `screen and (min-width:calc(640px - 1px)) and (max-width:calc(1680px - 1px))` |

**The paren fix is defensive, not load-bearing** — every query here has one feature per parenthesis pair, so with the
paren fixed and the `calc()` still truncated every query stays false. I had written the opposite into a patch README
before checking, and corrected it.

## 10. Row 0.19 — the space at a text run's edge

`TextLayout::layout` emits a space only between two words of its own run (`text_layout.cpp:116` discards it, `:137`
puts it back). A run that starts or ends in a space loses it, and the next inline box is placed flush against the last
word — `"often abbreviated as"` + `<a>browser</a>` read **"asbrowser"**.

It is symmetric. Counted in the article body: **424** boundaries of whitespace before an inline start tag, **316**
after an inline close tag.

| fixture | before | after |
| --- | --- | --- |
| `<p>a <a href="#">b</a> c</p>`, link x | 7.0 | **11.4** |
| the same, `"often abbreviated as"` run width | 176.2 | **181.3** |

Both halves were proved to fail **independently**: the whole fix reverted turns the trailing assertion red, and
disabling only the leading advance turns the leading assertion red. The third assertion in the block (a trailing space
before a block child) passes on the unfixed engine and is a guard, not proof — it says so in the test.

## 11. The page, and the number that went the wrong way again

| | before | after |
| --- | --- | --- |
| pixel agreement | 84.1% | **77.6%** |
| overflow count | 139 | **148** |
| `"often abbreviated as"` y | **900.0** (off screen) | **749.6** (on screen) |
| site subtitle y | 669.9 | **519.4** |

**The article moves up 150.4px and real article text enters the first screen.** That is row 0.17's stated goal,
measured rather than inferred.

The percentage fell because the shift pulls a `figure.mw-default-size mw-halign-right` into the viewport at
**x=46.4 w=953.6 h=175.4**, where chromium floats it at ~255 wide with the text flowing beside it. `float` is absent,
so the figure takes the whole content column:

| band | before | after |
| --- | --- | --- |
| 576-640 | 87.1% | **27.7%** |
| 640-704 | 66.1% | 43.7% |
| 704-768 | 35.9% | 49.3% |

That is row **0.20**, opened with those numbers. It is the fifth time a fix has moved the whole-page percentage the
wrong way while the layout got closer — 0.11, 0.15, 3m, 0.18 and now this. Read the row positions first, every time.

## 12. A documentation defect found and fixed

**Row 0.18's checklist row was never a row.** The insertion in the first batch matched an anchor whose preceding line
did not end in a newline, so the row was concatenated onto row 0.17's line: `... line box || 0.18 | ...`. The table
still rendered, the commit message claimed the tick, and `grep 0.18` found it. It is split onto its own line in this
commit, and the tier-0 table now validates at 22 rows with four columns each.

**The lesson is the same one this project keeps relearning: check the artefact, not the intention.** The check that
would have caught it is one line — every table row must start with `|` and contain exactly five pipes.

## 13. State at the end of this batch

| | |
| --- | --- |
| `main` | rows 0.17, 0.18, 0.19 closed; 0.20 opened |
| suite | **67/67 twice** (147.89 s, 130.46 s) |
| pixel agreement | **77.6%** (page 421,819 B HTML / 215,770 B CSS, unchanged from the first batch) |
| overflow count | **148** |
| rows open in tier 0 | 0.20, 0.14 (`[~]`), 0.1 (blocked) |
| next | **0.20** — `float`. It is the largest single difference on the page and it hides everything below it |

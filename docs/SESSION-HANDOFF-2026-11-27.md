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

# Session handoff — 2026-11-26

One commit: **a run of white space is not a flex item, and it has no minimum width.** Row 0.13's tail
closes; three narrower defects it exposed are named as new rows.

## 1. Where this session started

A fresh pod: `/scratch/work` empty, no repo, no upstream, no toolchain, no `build/`. `tools/setup.sh`
rebuilt all of it; the first build is 299 steps and took about 40 minutes on two cores, in eight chunks
under the 275 s limit. `python3 tools/check-build.py` then said **all 68 binaries current**.

The baseline had to be re-created too, because the reference page and its CSS lived only in the previous
pod's `/scratch/work/reference`:

| baseline | value | how |
| --- | --- | --- |
| `en.wikipedia.org/wiki/Web_browser` | 416,840 bytes | `curl` through the environment proxy |
| its stylesheets | 215,770 bytes, 2 files | the two `<link rel=stylesheet>` URLs, concatenated |
| pixel agreement | **81.1%** | `tools/reference-render.sh` |
| overflows | **141** | `tools/article-position.cpp --overflow` |

Both numbers reproduced the last session exactly, which is the only reason to trust the instruments again.

## 2. The probe could not say which number was wrong

`--overflow` named `header.vector-header` at **422.1px past**, inside `div.vector-header-container`
x=0 w=1024. Its six children:

| child | x | width |
| --- | --- | --- |
| anonymous text | 43.2 | 28.8 |
| `div.vector-header-start` | 88.0 | 429.2 |
| anonymous text | 533.2 | 28.8 |
| `div.vector-header-end` | 578.1 | 788.5 |
| anonymous text | 1342.0 | 38.4 |
| anonymous text | 1382.6 | 19.2 |

Two real items, four runs of white space, six gaps of 20px: 1,432.9px of items in a 901.3px content box.
The row was not shrinking enough, and the probe printed only the max-content width — which cannot say
whether an item is **bigger than its base** or **floored above the space it was given**. So the first
change of the session was to the instrument: `LayoutEngine::intrinsic_min_width()` is now public
(`layout.hpp:147`) and `article-position` prints `max=` beside `min=`.

## 3. What the pair of numbers said

```
    (anonymous) max=  28.83  min=  28.83  rect.w=  28.83  text
    div         max= 439.61  min= 429.20  rect.w= 429.20
```

`min == max` almost everywhere, including for runs of white space. Two defects, both in the same place:

1. **A run of white space was a flex item.** css-flexbox-1 §4: a contiguous sequence of child text runs
   becomes an anonymous item *unless the whole run is white space, in which case it is not rendered at
   all*. Upstream's predicate is `can_skip_is_anonymous_text_run` (`Rust/src/layout/layout_pass.rs:455`),
   used where items are generated (`flex_formatting_context.rs:1217`) and again for grid
   (`grid_formatting_context.rs:2180`). Here the four runs were items with widths **and** a gap on each
   side: 115.2px of width and 80px of gap belonging to nothing.
2. **Their minimum width was their measured width.** `min_content_width` split a text run on `' '` only,
   so `"\n\t\t\t"` — which contains no space — was one unbreakable word measuring 28.83px. A box that
   renders nothing had a floor of 9.61–28.83px, and every ancestor inherited it.

## 4. The fixture, because the page cannot say "correct"

A page cannot tell you what a browser would have done. Two 50px boxes 10px apart in a 200px row:

| | chromium | engine, before | engine, after |
| --- | --- | --- | --- |
| first box x | 0 | **19.6** | **0** |
| second box x | 60 | **99.2** | **60** |

`reference-render.sh` scored that fixture **99.7%** both ways: the boxes are small and the page is
otherwise white. The geometry is the evidence; the percentage is not. That is the same trap the last
session recorded twice, and it is worth restating: *a metric that cannot tell a bug from a design is worse
than no metric, and a page-sized percentage is a very blunt instrument for a 20px error.*

## 5. The change

| file | what changed |
| --- | --- |
| `src/rendering/layout.cpp` | `is_collapsible_whitespace_run()` (new) and its use in `layout_flex_children` and `layout_grid_children`; `widest_word` now breaks at any `isspace`, not only `' '` |
| `src/rendering/layout.hpp` | `intrinsic_min_width()` — the min-content width, exposed for the probe |
| `tools/article-position.cpp` | prints `max=` and `min=` for every node, because one number cannot say which is wrong |
| `Tests/AetherisRendering/TestAdvancedLayout.cpp` | two cases, both proved to fail without the fix |

## 6. Measured, before and after

| instrument | before | after |
| --- | --- | --- |
| overflow count | 141 | **137** |
| `header.vector-header` | worst offender, **422.1px past** | **not an offender at all** |
| worst offender | `header.vector-header` | `header.mw-body-header`, 252.5 past (a different element, deeper in the page) |
| pixel agreement | 81.1% | **81.2%** |
| header row | 1,432.9px of items in 901.3px | **932px in 928px** |
| whitespace run's min-content | 9.61 | **0** |
| suite | 67/67 | **67/67 twice** (127.78 s, 130.83 s) |

The header's children are now `vector-header-start` 309.3, a 20px gap, `vector-header-end` 602.7 — the
row fits, and the account links **paint**: "Donate", "Create account" and "Log in" are legible where the
last session recorded them clipped. Row 0.13's tail is closed.

## 7. Two tests, and both were checked against the unfixed engine

| case | asserts | fails without the fix? |
| --- | --- | --- |
| newlines between two flex items | second item at x=60, five children still in the tree | **yes** — "the first item starts at the row's left edge" |
| a nested row whose content is 20px plus a newline, in a 30px line | inner width exactly 20 | **yes** — "white space contributes nothing to the floor" |

The second case had a first version that measured `"ab\tcd"` against `"ab"` and **passed on the unfixed
engine**, because the tab is not measured either way. It was replaced, not kept: an assertion that cannot
distinguish the bug from its absence is worse than none, because it looks like proof.

## 8. What this does not fix, named as rows

| row | defect | why it is separate |
| --- | --- | --- |
| 3l | `.vector-header-end` does not push its content to the right edge | the links paint at x=386–952 where chromium puts them at 800–990; the row now fits, so this is alignment inside it, not overflow |
| 3m | `max_content_width` still measures a white-space run at its untransformed width | a newline measures 9.61px, so every container's base size carries its markup's indentation. Only the minimum was fixed here; the maximum feeds the flex base size |
| 3n | A lone flex item is never shrunk to its automatic minimum | `item.base = min(available_width, max_content)` makes free space zero for a single item, so the floor row 0.13 added is unreachable in that case |

## 9. Mistakes, recorded

- **A test that could not fail.** The `"ab\tcd"` case passed before and after. Caught only because the
  rule is to revert the fix and watch the test go red; the first version was replaced.
- **The first floor test's premise was wrong.** A single item in a 1px row did not exercise the shrink at
  all — its base is capped at the available width, so free space is exactly zero. The fixture needed two
  items before it tested anything.
- **`min_content_width` was private.** The probe needs it for the same reason it needs the max, so it is
  now exposed the same way (`intrinsic_min_width`), rather than the probe re-implementing it.
- **A build killed by the 275 s limit is not a failure.** `tools/setup.sh` was killed during its second
  `cmake` configure and reported nothing; the CMake cache was simply absent. Run the configure step on its
  own (~100 s each) rather than inside `setup.sh` with a full build behind it.

## 10. Second batch — row 3l turned out to be row 3m

Row 3l said `.vector-header-end` does not push its content to the right edge. It does: the element fills 602.7px
from x=349.3 and its `nav` child reaches x=952, the right edge. What was wrong was one level deeper - the nav's
`justify-content: flex-end` had **no free space to distribute**, because its own base size was 615.42px of which
**423.23px was content**: six runs of white space at 28.83, 38.44, 28.83, 28.83, 38.44 and 19.22. So row 3l was
row 3m, and fixing the maximum is what moved it.

| | before | after | chromium |
| --- | --- | --- | --- |
| `nav.vector-user-links` x | 386.3 | **752.2** | links at 800-990 |
| its max-content | 615.42 | **199.84** | — |
| header row | 1,432.9px in 901.3px | **932px in 928px** | — |
| document height | — | **33px shorter** | — |

The rule is the same one the minimum already used, applied to the other number: a white-space run adds nothing
inside a container that lays out a row of items, and one space anywhere else (css-text-3; upstream
`inline_formatting_context.rs:2931`, `:4546`). `tools/article-position.cpp` also learned `--all`, because a count
that moves by two cannot be explained by a list of fifteen.

### The two page-level numbers moved the wrong way, and the table says why

Agreement 81.2% -> 80.4%, overflows 137 -> 139. Per band:

| band | before | after | delta |
| --- | --- | --- | --- |
| 0-64 (header) | 88.0 | 88.7 | +0.7 |
| 128-192 | 94.9 | 97.0 | +2.1 |
| 192-256 | 98.2 | 92.7 | -5.5 |
| 512-576 | 85.1 | 46.4 | **-38.7** |
| 576-640 | 10.6 | 22.9 | +12.3 |
| 640-704 | 46.2 | 67.2 | +21.0 |

Moving everything up 33px moved a **pre-existing black rectangle** - the page title bar, painted black and placed
about 410px below where chromium puts it - across a band boundary. That rectangle is the largest visual defect left
on the page and is now row **0.14**. The two extra overflows are its children.

**The lesson, stated plainly: a whole-page percentage is dominated by whichever single defect is biggest.** Row 3m
cannot be judged by it. The numbers that judge it are the nav's x, its base size, and the fixture.

## 11. The environment stopped being able to build — read this before continuing

At the end of this batch the pod could no longer compile or link anything:

| symptom | value |
| --- | --- |
| `zig` on any file | `error: failed to evaluate zig clang ... failed to spawn: SystemResources` |
| zombies | **10,087** processes in state `Z`, all children of PID 1 (`python3`) |
| `ulimit -u` | 4096 soft, **4096 hard** — cannot be raised |
| `ninja` | 21 of 68 binaries could not be relinked; every link dies in `zig`'s LLD self-spawn |
| `git status` | fails on threaded lstat (the usual trap; `-c index.threads=1` still works) |

PID 1 does not reap, so the zombies only accumulate and a `kill -CHLD 1` changed nothing. **A pod recycle is the
only fix.** What that means for the work:

- `main` is **clean and green** at `c3c58c8a`. Nothing unverified was committed.
- The working tree holds row 3m: `src/rendering/layout.cpp`, `tools/article-position.cpp`,
  `Tests/AetherisRendering/TestAdvancedLayout.cpp` and this file's checklist rows, uncommitted.
- **Verified:** nine layout/CSS tests pass against the rebuilt library; the fixture measures 20.0px; the new
  assertions compile.
- **Not verified:** the suite has not run once, and the new assertions have never executed. Row 3m is `[~]`.

**Finished, 2026-11-26, after a pod recycle.** The new pod came back with an empty `/scratch/work` — the
project's assumption that it survives a recycle is wrong, and the only reason the work survived is that it had
been written to a patch and delivered. Recovered from it: repo re-cloned at `c3c58c8a`, patch applied cleanly,
toolchain reinstalled, 299 steps rebuilt in six chunks, `check-build` clean.

| check | result |
| --- | --- |
| `AetherisAdvancedLayout` with the change | **pass** |
| the same test with the `max_content_width` hunk reverted | **fails**: "and the newlines around it add nothing, because they generate no item" |
| `ctest --test-dir build -j 2` | **67/67 twice** (162.89 s, 151.90 s) |
| overflows, re-measured on the new pod | **139**, the same as recorded |
| agreement, re-measured | **80.4%**, and the same ten text rows |

So every number in §10 was reproduced first-hand on a different pod before it was committed. Row 3m is `[x]`.

**A note for the next recycle: `/scratch/work` did not survive this one.** Re-cloning, reinstalling the
toolchain and rebuilding cost about 45 minutes. Anything uncommitted must be delivered as a patch.

## 13. Third batch — row 0.14, and the pod ran out of processes again

The black band was not a layout defect. `tools/article-position.cpp` put `main#content` at y=546 and the title at
546.4, and a new probe — `cmds`, which prints every paint command in a y band — named the culprit in one line:

```
DrawShadow   x=24.0 y=546.4 w=976.0 h=40.6  color=black
DrawShadow   x=24.0 y=587.0 w=976.0 h=34.0  color=black
```

Two `DrawShadow` commands with the colour **black**, on boxes whose CSS says `box-shadow: none` and
`box-shadow: 0 1px #c8ccd1`. `render_tree.cpp` read the value positionally as `x y blur colour` and defaulted an
absent colour to black, so `none` became a black rectangle the size of the box.

| | before | after |
| --- | --- | --- |
| pixel agreement | 80.4% | **88.8%** |
| title bar | 976x78 black | white, title in black |
| toolbar | black, then a grey band | white with a 1px line |

Two fixes: the value is parsed by kind (css-backgrounds-3 §7.2: a list, `none` is empty, colour anywhere,
`currentColor` default, two lengths minimum), and an outer shadow is clipped to outside its border box (§7.2.1),
which is what removed the grey band the first fix exposed.

### The assertions, and the one that could not fail

`AetherisAdvancedPainting` pins both halves, and each was proved to fail without its own fix. The clipping one
took three attempts: as a document render it passed on the unfixed engine, because a box with its own background
paints over its shadow - the case that distinguishes is a box with **no** background, which is the toolbar's. It
now calls `draw_shadow` on a bare surface, where the answer cannot be confused with "nothing was painted".

### Finished, on a third pod

The pod this was written on filled its process table (11,292 zombies) while the source was being re-linked after
the proof-by-reverting cycle, so it could not build, test or even push. The work went to a branch as a patch. A
recycle came back with an **empty `/scratch/work`** again, the patch was re-applied with `git am`, and the tree was
rebuilt from scratch:

| check | result |
| --- | --- |
| `check-build.py` | all 68 binaries current |
| `AetherisAdvancedPainting` on this source | **pass** |
| `ctest --test-dir build -j 2` | **67/67 twice** (196.89 s, 186.03 s) |

Row 0.14 is `[x]`. The 88.8% agreement was measured on the previous pod from byte-identical source (the patch was
re-applied unchanged), and reproduced 80.4% -> 88.8% there after the fix.

**And the other half of the row is a different defect**: the article starts 410px too low because the **table of
contents** is laid out in the content flow (a 988x350 box above `main`), not in the start column. That is row
**0.15**, opened with the numbers.

## 14. Fourth batch — row 0.16, and a premise that did not survive the render

Row 0.15 said the table of contents was wrongly in the content flow. The fix for it was obvious and wrong. Two
things were done in order.

**First, the shorthand.** `.mw-page-container-inner` sets its grid only through `grid-template`, and the engine did
not read it — a container reporting `display=grid` with every child in one column. `layout_grid_children` now falls
back to the shorthand for whichever longhand is absent (splitting on the top-level `/`, not one inside `minmax`),
and `parse_tracks` reads `minmax(a,b)`. Fixture: longhand puts two grid children at x=0 w=100 and x=100 w=924; the
shorthand did the same after the fix, and x=0 w=1024 / x=1024 w=0 before. Suite **67/67 twice**. Agreement on the
reference page: **88.8% before and after** — the rule that needs the shorthand is behind `min-width:1120px` and the
reference render is 1024px wide, so this closes a compatibility gap without moving this page.

**Then the render, which corrected the row.** At 1024px chromium also stacks the Contents list above the title —
its heading at y=98, the engine's at y=186, same order. The grid that would put the TOC in a column is behind
`min-width:1120px` and correctly does not match. So row 0.15's explanation was wrong even though its number was
right: `main#content` starts at **y=546.4** against chromium's **356**, and the 190px is **+88px in the block above
the Contents heading** (the header block is 156px tall against ~68px, with the failed-logo block at h=172.9) plus
**+103px in the Contents box** (engine 180.1 h=350.4, chromium 98-345). Row 0.15 now says that instead.

**The lesson, and it is the same one as row 0.14's:** a number with the wrong explanation attached is worse than an
open row, because the explanation is what the next person builds on. The render was already in the tree; comparing
it cost nothing.

## 12. Next

1. **Finish row 3m** (§11): build, run `AetherisAdvancedLayout` both ways, suite twice, tick, commit.
2. **Row 0.14** — the black title bar, 410px too low. It is the worst band on the page by a factor of two and it
   currently hides whatever else moved. Crop both renders at y 500-700 first; the paint question ("why black") and
   the placement question ("why here") are separate and only one of them is row 0.8's.
3. **Row 3n** — a lone flex item is never shrunk to its minimum, because its base is capped at the line width.

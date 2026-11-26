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

## 10. Next

Row 0.13's tail is closed, so the tier-0 order is empty of open work except **3l**, which is the same
header: `.vector-header-end` has `flex-grow: 1` and fills 602.7px, but its `nav` child sits at x=386
instead of at the right edge. The probe question is `class=vector-header-end`, and the thing to read
upstream first is how `justify-content`/`margin-left: auto` reach a nested row's end
(`flex_formatting_context.rs`, main-axis alignment).

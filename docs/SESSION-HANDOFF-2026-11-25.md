# Session handoff — 2026-11-25

## 1. First commit — the 22px, bounded and then left alone

Row 0.10's tail: the real page's `h1` sits at x=487 with a width of 534.84 in a container ending at
1000, so its right edge lands at 1021.84 - a 21.84px overshoot.

What this session added is a bound on the question rather than another attempt at the answer:

- The overshoot is **not** padding or border; those were fixed on 2026-11-24 and the fixture proves it
  (`padding: 0 10px` at `flex-end` moved from 260-319 to 240-299).
- The flex free space is computed from a base of **513**, and the placed box ends **534.84** wide. The
  base size and the placed box disagree by exactly the overshoot.
- `tools/article-position.cpp` now also prints the node's text-fragment count, and it says the `h1` has
  **0 fragments of its own** - its text is in a child. So `max_content_width()` (which measures text)
  and the child's actual layout are two different numbers for the same content.

The next question is which measurement is wrong, and the way to answer it is a fixture with one text
item in a flex row, comparing `rect.width` against `measure_text` of the same string - not another look
at the page.

### Why this thread was stopped

Three attempts, 22 pixels, one heading. The engine's real remaining gap is not this: it is that nobody
can say whether a render *matches* anything, because there is no reference to compare against. Every
defect found this week was found by a number contradicting an expectation, and the numbers are running
out of things to contradict. A reference render is the next instrument worth building.

## 2. Second commit — a reference render, because the numbers ran out of expectations

The week of 2026-11-23 found seven defects, every one because a number contradicted an expectation. By
the end of it the expectations were the scarce thing: the page rendered, and nothing could say whether
it rendered *right*.

### Chromium was already installed

`which chromium` finds one, so the reference is a real browser rather than a hand-written guess.
`tools/reference-render.sh`:

1. builds one **self-contained** document - the page's stylesheets inlined, its scripts dropped - so
   neither side needs the network and the comparison is about layout, not JavaScript;
2. renders it in chromium at 1024x768;
3. renders it in the engine at the same viewport;
4. reports sampled pixel agreement and the rows where link-coloured text lands in each.

### The first number

| | |
| --- | --- |
| pixel agreement, `en.wikipedia.org/wiki/Web_browser` | **44,390/49,152 = 90.3%** |
| contents-list item spacing, chromium | **27-28 px** |
| contents-list item spacing, engine | **79-80 px** |
| a minimal `<ul><li>` in both | **99.8%**, 19-20px on both sides |

So the largest visible difference is a spacing rule on the contents list that the engine applies at
roughly three times its size, and it is not list layout in general - the minimal list agrees almost
exactly. That 79px is what pushes the article title and body below the fold, which is the thing the
screenshots have been showing all week.

### What this changes about the work

Until now each defect was found by noticing something odd. From here the question is a number that goes
up or down: 90.3% today, and whatever it is after the next fix. That is a far better guide than reading
a render, and it costs about a minute per page.

### Next step, named in row 0.11

Extend `tools/article-position.cpp` to report the rules that set `margin`/`padding`/`line-height` on a
node named on the command line, and ask it about a contents item - the same way the `h1` at x=1000 was
explained by printing its ancestor chain.

## 3. Third commit — row 0.11 narrowed to an anonymous line box

The reference said the contents list sits 79-80px apart where chromium puts it 27-28. The probe was
extended to point at a node (`id=<id>` or `tag=<tag>`), print its box, its children with `display`,
`line-height` and height, and the rules that change it.

### What it ruled out

| question | answer |
| --- | --- |
| Is it a margin on the list item? | No - the rules only add `margin: 0.2em 0`, about 3px |
| Is it the anchor inside? | No - the `<a>` is 45.6px in a 79.2px item |
| Is it list layout in general? | No - a minimal `<ul><li>` agrees with chromium at 99.8% |
| Is it whitespace text nodes? | No - skipping them changed nothing (33.6px either way), so that change was reverted |

### What it found

A 156-byte fixture reproduces the defect: a `<li>` holding one block `<a>` is **33.6px where chromium
gives 16.8** - exactly double. The children print shows the doubling is not the DOM: the `<li>` has a
**second inline child of 16.8px with no DOM tag at all**. The engine is producing a line box with no
node behind it.

The suspect is the tail of `layout_inline_children`:
`cursor_y = std::max(max_bottom, cursor_y + line_height)` - it adds a line whether or not anything was
placed on it. That is the next thing to read.

### The change that was reverted

Skipping whitespace-only text nodes is the obvious fix and it did not move the number. It was reverted
rather than shipped: a plausible-looking change that leaves the measurement identical is a guess, and
this engine has already been bitten by one of those (the `reserve(24)` that measured 33% slower).

## 4. Fourth commit — the double line, and a metric that went the wrong way

The suspect named in the previous commit was the tail of `layout_inline_children`:

```cpp
cursor_y = std::max(max_bottom, cursor_y + line_height);
```

It closed an open line unconditionally. After a block-level child, `cursor_y` is already that block's
bottom - so the tail added a second line on top, and every block inside an inline formatting context was
counted twice. A `<li>` holding one block `<a>` measured **33.6px where chromium gives 16.8**.

The function now tracks whether a line is actually open and closes only that.

| | before | after | chromium |
| --- | --- | --- | --- |
| `<li>` with one block line | **33.6px** | **16.8px** | 16.8px |
| contents-list spacing, real page | **79-80px** | **28-29px** | 27-28px |

`AetherisAdvancedLayout` pins it by comparing the item's height with the block's height rather than a
hard-coded 16.8 - so the assertion stays true at any font size.

### The metric went down and the layout got better

Pixel agreement fell from **90.3% to 76.9%**. That is not a regression; it is the metric. The earlier
score was mostly blank page matching blank page, and compacting the contents list moved real text into
areas where the reference has the article title and body. A fix that brings content into view will often
lower this number while making the page more correct.

So the tool prints both, and they must be read together: **agreement** says "is the same content in the
same place", **row positions** say "is the spacing right". Neither alone is the answer, and a single
percentage would have made this commit look like a regression.

### What is left at the top of the page

The header - logo, "Donate / Create account / Log in" - renders as an empty strip in the engine where
chromium puts it at y=16-46. That is now the largest remaining difference, and it is the next question.

## 5. Fifth commit — the header, and `alt` text for an image that fails

The reference render left one large difference: the header. Chromium puts the Wikipedia wordmark and
"Donate / Create account / Log in" in the top 46 pixels; the engine painted an empty strip.

`tools/article-position.cpp --top` (new) lists every layout node starting above a given y, in document
order. It settled it in one run:

- the header **is** laid out - `div` flex at y=0 h=66, `header` at y=0 h=66 w=949;
- the logo images **are** laid out - `img y=14 h=22 w=140` and `img y=41 h=11 w=140`;
- they are simply never painted, because their URLs (`/w/skins/...`) do not resolve when the page is
  rendered from a local file.

And chromium, in the same situation, draws the **alt text** - which is why the reference shows
"Wikipedia" and "The Free" where the logo would be. The engine drew an empty box.

`RenderTree` now emits a `DrawText` carrying the `alt` attribute when an `<img>` has no loaded image,
with the element's own font size and colour, and emits nothing when there is no `alt`.

| | before | after | chromium |
| --- | --- | --- | --- |
| failed logo | empty box | "Wikipedia" / "The Free Encyclopedia" | "Wikipedia" / "The Free" (clipped) |

### A note on what moved and what did not

Pixel agreement barely changed: 76.9% to 76.6%. The header is a small area, so a percentage over the
whole viewport cannot see it. The picture is the better evidence for this row, and that is worth
remembering about the metric - it is a guide, not a verdict.

## 6. Row 0.13 opened — the account links are zero width

With the header fixed, the next difference the reference shows is the account links: chromium puts
"Donate / Create account / Log in" at the top right, the engine puts nothing there.

It is not a missing element. `tools/article-position.cpp` finds `p-vector-user-menu-overflow` in the
layout tree at **x=766.5 y=8 w=54.1**, with its flex `ul` beside it. The items are what fails:

| node | width | height |
| --- | --- | --- |
| `p-vector-user-menu-overflow` | 54.1 | 33.6 |
| `pt-sitesupport-2` (Donate) | **0.0** | 33.6 |
| its `<a>` | — | 16.8 |
| its `<span>` holding the word | — | **0.0** |

The text never laid out, so nothing paints. No rule sets a width - the only ones that apply are
`margin: 0.2em 0` and `margin: 0 4px` - so the zero comes from the flex base size, which means
`max_content_width()` returns 0 for these items.

**The minimal fixture does not reproduce it.** `<ul display:flex><li><a><span>Donate</span></a></li></ul>`
gives the item **50.9px**, which is right. So the trigger is in this page's markup, and the leading
candidate is an anonymous box: the `<span>`'s text is one of the children with no DOM node behind it -
the same class of thing row 0.11 turned up - and `max_content_width()` measures text only when the node
has a DOM text node.

### Next step, named

Expose `max_content_width()` to the probe (it is a private static) or replicate it there, and print what
it returns at each level of one of these items, next to the text's measured width. That turns "0" into
"0 because this box has no DOM node" and the fix follows.

## 7. Sixth commit — the flex shrink had no floor, and the probe corrected me

The row's own next step was to expose the sizing and see what it returns. That was the right move,
because it **falsified my hypothesis**.

I expected `max_content_width()` to return 0 for these items - an anonymous box it could not measure.
Instead:

```
li       intrinsic=  59.30  rect.w=   0.00
  a        intrinsic=  50.89  rect.w=   0.00
    span     intrinsic=  50.89  rect.w=   0.00
```

The base size was **right**. The item was then shrunk to zero: the flex line had 54.1px and the items
totalled about 180. css-flexbox-1 floors an item's shrink at its **automatic minimum size**
(`min-width: auto`, the min-content size), and the engine had no floor at all.

`LayoutEngine::min_content_width()` now answers it - a text run's widest word, the widest of a block's
children, or the sum of a row of inline ones - and the shrink pass floors there.

| | before | after | chromium |
| --- | --- | --- | --- |
| menu item width | **0.0** | **43.3** | ~43 |
| "Donate" painted | no | yes | yes |
| pixel agreement | 76.6% | **81.1%** | — |

That is the first fix today that moved the agreement number in the right direction, which is worth
noting: the double-line fix lowered it while improving the layout, and this one raised it.

### What is left, and it is one level up

The items no longer collapse, but they now **overflow** to the right - "Create account" runs past the
viewport - because the outer container still allocates only 54.1px. So the next question is what sizes
*that* box. It is the same shape of question as this one, and the probe now has the tool for it.

## 8. Seventh commit — an experiment that was reverted, and what it proved

Row 0.13 left the menu items overflowing, because the container allocated them only 54.1px. The
container is `.vector-user-links`, which is `display: flex` - and the engine's intrinsic widths measured
a flex row as its **widest child** instead of the **sum** of its items. Summing them is what css-flexbox-1
says, and it worked as intended:

| | before | with the row-sum |
| --- | --- | --- |
| container intrinsic width | 54.1 | **423.23** |
| container actual width | 54.1 | **389.55** |

The box was finally the right size. And the page looked **worse**: the links were pushed further off the
right edge - only "Don" visible, where "Donate" had been - and pixel agreement did not move, 81.1%.

So it was reverted. The parent still places the child at x=766, using the size the child had *before* it
was laid out; giving the child its correct width without fixing that placement just moves the overflow.
A spec-correct change that makes the page worse is not a fix, and this engine has now had two of them
(the `reserve(24)` that measured slower, and this).

### The next question, named

In `layout_flex_children`, place each item from the width it **ends up with** rather than from its base
size. That is the same stale-size question one level up, and it is what the row now says.

## 9. Eighth commit — three variants, three regressions, and a stop

I proposed going after the *pattern* rather than the symptom: a box sized or placed from a value
computed before the thing it depends on was known. The attempt was worth making and it failed, and the
record matters more than the result.

### What was tried

1. **The row-sum.** A flex row's intrinsic width is the sum of its items, not the widest of them.
   Correct per css-flexbox-1, and it worked: the container went from 54.1px to **423.23 intrinsic,
   389.55 actual**. The page looked **worse** - links pushed further off the right edge.
2. **`translate_subtree` + re-placement.** A new helper so a flex container can move an item after its
   own layout reveals its real width, and a pass that re-places the line from those widths. With it the
   container moved from x=766 to **x=919.1, w=389.6** - a right edge of **1308 on a 1024px page**.
   Worse again.
3. **Both together.** Worse still: only a fragment of "D" was visible.

All three were reverted. Pixel agreement never left 81.0-81.1% for any of them, which is its own signal:
none of these changes touched the thing that is actually wrong.

### What the measurement says

The box is now sized correctly and positioned wrongly. Its position is decided **above** it - the parent
places it using the width it had before layout - and the re-placement I wrote did not reach that level.
So the defect is in the header chain, not in the menu, and the next step is to walk
`p-vector-user-menu-overflow` upward with the probe and find which ancestor assigns the x.

### Why this is the place to stop

Four sessions of this pattern have produced a reliable rule: the defect is one layer below where the
symptom appears. This time I guessed which layer and was wrong twice. The instrument that would settle
it is the ancestor walk the probe already has, pointed at the right node - which is a deliberate piece of
work, not a patch. Better to leave the tree green and the question sharp than to ship a fourth variant.

## 10. Ninth commit — the mechanism, measured at last

The previous commit stopped with "walk the ancestor chain before touching code". This one did that, and
the walk explains the whole thing in one table.

### The chain, from the menu container upward

| node | x | width | ends at | its parent's right edge |
| --- | --- | --- | --- | --- |
| `nav` (`justify-content: flex-end`) | 678.7 | 192.2 | 870.9 | — |
| its child | **697.9** | 192.2 | 890.1 | 870.9 — **19px past** |
| `p-vector-user-menu-overflow` | **861.2** | 117.7 | 978.9 | 890.1 — **89px past** |

Every level overflows by the gap between **the size the parent handed its child** and **the size the
child ended up with**, and the overflow accumulates down the chain.

### The fourth variant, measured rather than eyeballed

Re-placement alone - `translate_subtree` plus a pass that repositions the line from the real widths -
moved the menu **861.2 → 884.2**, i.e. **23px further right**. Previous variants were judged by looking
at a crop; this one was judged by a number, and the number said no.

All four variants are reverted. Pixel agreement never left 81.0-81.1% for any of them.

### What that rules out, and what it means

The fix is **not** in how one flex container places its own items. Each container is placing correctly
given the size its parent handed it; the parent hands it a stale size. A re-placement pass inside one
container cannot fix that, which is why all four attempts failed the same way.

So the work is a **restructure**: a container can only place its children once its own width is final,
which means either a two-pass layout (measure top-down, place bottom-up) or a parent re-placing after
its children settle. That is not a patch, and pretending otherwise has cost four attempts.

## 11. Tenth commit — an instrument for the restructure, and a variant rejected by it

The previous commit ended with "the work is a restructure, not a patch". A restructure needs a number,
so this one built the number first.

### `--overflow`

`tools/article-position.cpp --overflow` walks the layout tree and counts containers whose children reach
past their own content box, worst first.

**Baseline on `en.wikipedia.org/wiki/Web_browser`: 165 containers.**

| container | children past its content box |
| --- | --- |
| `header.mw-body-header` | **347.7px** |
| `div.vector-header-start` | **321.2px** (in a 188px box) |
| `div#p-lang-btn` | 232.0px |
| `div#right-navigation` | 218.8px |

So the header is where the page breaks, and the account menu is a symptom of it, not the cause. That is
the first time this has been stated as a number rather than an inference from a screenshot.

### The fifth variant, rejected by the number

Re-placement - `translate_subtree` plus a pass that repositions a flex line from the widths its items
actually got - was tried again, and judged by the count instead of by a crop:

**165 → 164.**

That is noise. The change is reverted, and the rejection took one measurement instead of a session of
squinting at pixels. The two earlier variants were rejected by eye and by x-coordinate; this one by the
metric the fix is supposed to move.

### What this leaves

The restructure is now a bounded, measurable job: a container can only place its children once its own
width is final, so layout needs a measure pass and a place pass, and **165 is the number to bring down**.
Whoever picks it up next has a baseline, a list of the worst offenders, and four documented dead ends.

## 12. Eleventh commit — upstream says the engine is already right, so the metric must be wrong

The instruction was to check upstream first, so the reference tree was re-cloned (it does not survive a
pod recycle - `tools/setup.sh` should grow a step for it) and read.

### What upstream actually does

`Libraries/LibWeb/Rust/src/layout/flex_formatting_context.rs`:

- **:4299** - the hypothetical main size is `clamp(flex_base_size, clamp_min, clamp_max)`, where
  `clamp_min` defaults to `automatic_minimum_size_for_clamping(...)`, i.e. the automatic minimum size.
- **:4318** - `resolve_flexible_lengths()` runs in `run()` **before** any placement, so every item's used
  main size is fixed before the line is positioned.
- **:4304** - `run()` is the sequence: prepare items, collect lines, resolve flexible lengths, cross
  sizes, then place.

The engine already has both halves: `min_content_width()` is the automatic minimum (row 0.13), and the
flex pass computes each item's width before placing it. So the port found nothing to port, which was
itself the answer.

### And enforcing the second half changed nothing

Adding the resolved-size discipline - the item keeps the size the algorithm resolved, and content
overflows inside the box - moved the overflow count by **zero: 164 to 164**. Reverted.

### The finding: the metric counts design as defect

Looking at the top offenders rather than the total explains it:

| container | x | note |
| --- | --- | --- |
| `div#p-lang-btn` | **1069.9** | beyond a 1024px viewport |
| `span#mwBEg` | **1009.4** | beyond a 1024px viewport |

Those are hidden menus and off-canvas UI, positioned outside the viewport on purpose. That is correct
CSS, not overflow. The counter treated "child reaches past its parent" as a defect without asking
whether the child was meant to be there.

So the instrument is wrong, not the layout - and six variants failed against it because it could not
distinguish a bug from a design. **Next step:** make `--overflow` ignore children whose box lies wholly
outside the document's viewport, then re-baseline. The real number will be lower, and whatever remains
will be worth chasing.

## 13. Twelfth commit — fix the instrument, then a real defect falls out of it

The previous commit ended with "the metric is wrong, fix it first". This one did, and the corrected
metric immediately named a genuine bug.

### The instrument

`--overflow` now ignores children whose box lies **wholly outside the viewport**. Hidden menus and
off-canvas UI are placed there on purpose - that is correct CSS, not overflow. Corrected baseline:
**164 -> 150**.

### Upstream had already answered the layout question

`Rust/src/layout/flex_formatting_context.rs:4318` resolves every item's used main size before placement,
and `:4299` clamps the hypothetical main size at the automatic minimum. The engine already did both, so
there was nothing to port - which is why six variants moved nothing.

### The first real offender

`div.vector-header-start` is 188px wide with children **321px** past. The cause: **an `<img>` contributed
no width at all** to the intrinsic sizing. It has no children and no `width` style, so a link wrapping a
140px logo was measured as if it held nothing.

Upstream takes a replaced element's contribution from its natural size
(`replaced_sizing.rs:85-100`); the engine now uses the `width` attribute, or the loaded image's width,
for both max-content and min-content.

| | before | after |
| --- | --- | --- |
| overflow count | 150 | **147** |
| pixel agreement | 81.1% | 81.1% |

`AetherisAdvancedLayout` pins it: a link wrapping a 140px image is at least 140 wide.

### What is left

`div.vector-header-start` is still the worst offender at 321px, so the logo's container is the next thing
to walk - and the walk now starts from a metric that means something.

## 14. Thirteenth commit — two elements with the same class, and a probe bug of my own

Chasing `div.vector-header-start`, the worst offender at 321px. Three things came out of it, and none of
them was the fix I expected.

### A probe bug: an empty tag matched the document root

`find_layout` returned the first node whose `dom_node->name == tag`, and with `tag` empty that matched
the synthetic root, whose name is also empty. So `class=...` searches silently reported the root. One
line: `!tag.empty()`.

### The probe can now find an element by class, and prints widths

Without widths in the child print, "which child is 321px past" was unanswerable. It prints x and width
now, at both levels.

### The namesake

With those two fixed, the walk says the instance it found overflows by **54.7px**, not 321.2:

| child | x | width | right edge |
| --- | --- | --- | --- |
| anonymous inline | 88.0 | 38.4 | 126.4 |
| `nav` | 146.5 | 116.2 | 262.7 |
| anonymous inline | **282.7** | 48.0 | **330.7** |

The container's content box ends at 276. So this instance overflows by 54.7px. Wikipedia ships **two**
elements with that class - the normal header and the sticky one - and the scan reports the worst across
both. The class name is not an identity.

### What that means for the next step

`--overflow` needs to print an element's position and its parent's, so the worst offender can be told
apart from its namesake. Otherwise the next session will walk the wrong node exactly as I did - and this
is the third time in this row that the instrument, not the layout, was the thing that was wrong.

# Session handover — 2026-11-23

Two rows closed: **2h, one metric for layout and paint**, and **2e3, `text-align: start`/`end` follow
`direction`**. Suite **65 tests, 65/65, twice**, 127.55 s and
126.95 s at `ctest -j2`. No new test binary: the assertion went into `AetherisShaping`, which already owns
"a font is loaded and measure reflects shaping".

The row was found on 2026-11-22 while wiring bidi through layout: fragments were being *placed* by one
metric and *drawn* by another.

---

## 1. What shipped

| what | where |
| --- | --- |
| `TextLayout::measure_text` returns `Font::measure()` — the shaped width — plus letter- and word-spacing | `src/rendering/text_layout.cpp` |
| Two assertions: layout width equals the shaped width, and that width carries the kerning | `Tests/AetherisRendering/TestShaping.cpp` |

One function, one call site of the metric. Nothing else moved.

## 2. Numbers that were measured, not assumed

All at 16 px with the engine's embedded font, comparing the old sum of code point advances against the
shaped width the pen advances by:

| text | sum of advances (before) | shaped (after) | delta |
| --- | --- | --- | --- |
| `"To"` | 19.5625 | **16.8594** | −2.7031 |
| `"AV"` | 21.8906 | 20.8906 | −1.0000 |
| `"office"` | 44.1406 | 43.9062 | −0.2344 |
| `"The quick brown fox jump"` | 207.8750 | 207.1406 | −0.7344 |
| `"ToToToTo"` at 16 px | 78.2500 | 67.4375 | −10.8125 |
| `"ToToToTo"` at 40 px | 195.6250 | 168.4375 | −27.1875 |

Rendered, centred, 40 px, page 400 px wide: ink centre **199.0** against a page centre of 200. Before, the
box was 195.625 px for 168.4375 px of ink, so the pen started at 102.19 and the ink centre landed at
186.4 — 12.6 px left of where it belongs.

## 3. The errors, and what each cost

| first reading | what was true | what settled it |
| --- | --- | --- |
| "layout and paint already agree: every delta is 0.0000" | the probe set `AETHERIS_FONT` *after* the first `active_font()` call, so all four tables measured the bitmap fallback | running each font as its own process; the deltas appeared |
| "the difference is the ligature" | the ligature is the small part; **kerning** is the whole story — `"To"` loses 2.7 px and has no ligature | the per-string table |

The first one is the third appearance of the `active_font()` trap in this repo's history. It is now in the
probe's own comment.

## 4. Build facts a new session needs

| fact | consequence |
| --- | --- |
| the Debug library is compiled with UBSan (`__ubsan_handle_*` symbols) | a harness linked against `build/lib` needs `-fsanitize=undefined` too, or the link fails on those symbols |
| `RenderDocument::create(html, css, w)` ignores a page's `<style>` | pass the CSS explicitly in a harness; only the browser path collects `<style>` (`src/main.cpp:1858`) |
| `active_font()` is loaded once per process | one font per process, or a probe silently measures the bitmap fallback |
| `--render file:///…` has no backend | the CLI registers only HTTP; a 40-line harness on `RenderDocument` is the way to look at a local page |

## 5. Open work, in the order I would take it

| row | item | why here |
| --- | --- | --- |
| 2f | line breaking by cluster (`HB_GLYPH_FLAG_UNSAFE_TO_BREAK`) | the break pass still splits on whitespace |
| 2g | widen the shaped-run key before letter-spacing | the key has no spacing bits, so letter-spacing is measured but not shaped |
| 3a6 | `content` as a real list type, with alt text | unchanged |
| 1g4b | `pushState`/`replaceState`/`history.state` | unchanged |
| 0.1 | tier-0 coverage corpus | **blocked**: no open internet |

## 6. What this session would do differently

1. **Set the precondition before the first call, not after it.** `active_font()` caches its answer; a probe
   that sets the variable afterwards measures the fallback and reports perfect agreement.
2. **Distrust a table of zeros.** Four identical columns is a measurement that did not happen, not a
   system in perfect harmony.
3. **A row found while doing another row is worth writing down immediately.** 2h was visible on 2026-11-22
   and cost one function the next day; had it not been written down it would have been rediscovered as
   "Arabic text sits slightly wrong in its box".

---

## 7. Second row, same day — 2e3: `text-align: start`/`end` follow `direction`

| what | where |
| --- | --- |
| `start` is the left edge in LTR and the right in RTL; `end` the reverse; `left`/`right` ignore direction; an unset value is `start` | `src/rendering/text_layout.cpp` |
| Eight assertions on exact positions | `Tests/AetherisRendering/TestBidi.cpp` |

Upstream's rule, read rather than guessed: `LibWeb/HTML/Canvas/Canvas2DContextBase.cpp:445-470` writes it out
for canvas ("In RTL, `start` means right-aligned", "Explicit `left` and `right` alignments ignore direction"),
and `CSS/ComputedValues.h:447` gives the initial value as `TextAlign::Start`.

### Numbers

The bitmap fallback makes the assertions exact — 10 px per character at 16 px, so "abc" is 30 px in a
200 px line:

| text-align | direction | fragment x |
| --- | --- | --- |
| unset | ltr | 0 |
| unset | rtl | **170** |
| `start` | rtl | 170 |
| `end` | rtl | 0 |
| `end` | ltr | 170 |
| `left` | rtl | 0 |
| `right` | ltr | 170 |
| `center` | rtl | 85 |

Rendered: the `direction: rtl` paragraphs of the bidi fixture moved from the left margin to the right one,
and the LTR paragraphs did not move.

### What is still not there

`text-align: justify` is treated as `start`. Nothing else changed, and the suite's pixel tests did not move.

### What this row would do differently

Nothing. It was written from the upstream rule with the assertions first, and it took one build.

---

## 8. Third row, same day — 2f: line breaking by cluster

| what | where |
| --- | --- |
| `ShapedGlyph` carries HarfBuzz's flags | `src/rendering/shaper.hpp`, `shaper.cpp` |
| `safe_break_offsets()`: cluster starts not flagged `HB_GLYPH_FLAG_UNSAFE_TO_BREAK`, ascending, end of text last | `src/rendering/shaper.cpp` |
| `break_word()` + `allows_breaking_inside_words()`: a word too long for a line breaks at those offsets, only under `overflow-wrap: break-word\|anywhere` or `word-break: break-all` | `src/rendering/text_layout.cpp` |
| Assertions for the ligature, the kerned pair, the combining mark, and overflow-by-default vs break-word | `Tests/AetherisRendering/TestShaping.cpp` |

Upstream: `Libraries/LibWeb/Rust/src/layout/text_chunker.rs` splits chunks with an ICU `LineSegmenter` and a
`GraphemeSegmenter`; neither is available here, and HarfBuzz's own unsafe-to-break flags answer the same
question for the positions this engine can break at. `hb_glyph_info_get_glyph_flags()` needs no buffer flag
in HarfBuzz 15 — the flags are produced by default (the old `HB_BUFFER_FLAG_PRODUCE_UNSAFE_TO_BREAK` no
longer exists).

### Numbers

Embedded font, 16 px, offsets in bytes (`!` = unsafe to break at that cluster):

| text | clusters | safe offsets |
| --- | --- | --- |
| `office` | `0 1 4 5` — the ffi ligature covers bytes 1-3 | **1, 4, 5, 6**; never 2 or 3 |
| `To` | `0 1!` — a kerned pair | **2** (the end) |
| `AV` | `0 1!` | **2** |
| `fi` | `0` — one glyph | **2** |
| `e` + U+0301 + `x` | `0 0 3` | **4**; never 1 |

Control: `ox`, `To` and `AV` carry the flag; `ll`, `ab` and `of` do not — so the `To` assertion discriminates
kerning rather than passing on every input.

Suite **65/65 twice**, 122.91 s and 123.00 s. The new assertions sit inside `AetherisShaping`, so the count
stays 65.

### The environment died mid-row, and the row was verified twice

While linking, this session's environment exhausted its process table — 17,879 zombie processes parented to
PID 1 against a `ulimit -u` of 4096 — and every compiler invocation afterwards failed with
`failed to spawn: SystemResources`, down to `int main(){}`. `git` needed `-c core.preloadIndex=false` (its
threaded lstat failed). The tree was reverted to `3f00eaaa` and the work saved as
`/scratch/work/row-2f-cluster-breaking.patch` plus copies in `/scratch/work/row-2f-copy/`.

The patch was then verified in a **fresh sandbox**: applied with no fuzz to `3f00eaaa`, built (8 `ninja`
calls, 287 targets), `AetherisShaping` green, `ctest -j2` 65/65 twice (123.48 s, 128.74 s), and the probe
above returning exactly those offsets.

Then this session came back on a new environment, the patch applied cleanly a second time, and the suite ran
here as well: `AetherisShaping` green, 65/65 twice, 122.91 s and 123.00 s. Two environments, same result.

### Corrections to earlier notes

The 2e and 2e2 notes said "the reordering is in `Rust/src/layout`, which this tree does not vendor". **The
Rust layout is vendored** at `Libraries/LibWeb/Rust/src/layout/` — I looked in the wrong place (`Rust/src` at
the repo root). What that directory does *not* contain is a bidi reordering pass: `text_chunker.rs` splits
chunks by `text_type`, and `inline_level_iterator.rs:168` only decides whether a block is unidirectional
left-to-right. So the conclusion (the ordering had to be written here) was right; the reason given for it
was wrong.

### What this row would do differently

1. **Check the environment before trusting a failure.** `ps` would have shown the zombie wall before three
   failed links looked like a compiler bug.
2. **A patch file is a cheap insurance policy.** `git diff` into `/scratch/work` took ten seconds and made
   the whole row recoverable when the tree had to be reverted.

---

## 9. Fourth row, same day — 2g: letter- and word-spacing

| what | where |
| --- | --- |
| `shape_run()` takes `letter_spacing` and `word_spacing`, adds them to each glyph's advance and keys the cache on both | `src/rendering/shaper.{hpp,cpp}` |
| `Font::measure(text, letter, word)`; `TextLayout::measure_text` stops adding spacing itself | `font.{hpp,cpp}`, `text_layout.cpp` |
| `PaintCommand` carries the two values and `draw_text` passes them to the shaper | `render_tree.{hpp,cpp}`, `paint_executor.cpp`, `software_surface.{hpp,cpp}` |
| Assertions: spacing reaches the advances, the cache separates spaced from unspaced, the fragment is as wide as the run | `Tests/AetherisRendering/TestShaping.cpp` |

Upstream applies spacing while building the glyphs, not while measuring: `LibGfx/TextLayout.cpp:283-292` adds
`letter_spacing` to every glyph and `word_spacing` to glyphs whose cluster starts at a word separator
(`is_word_separator()`: space, no-break space, line feed), and its `ShapingCacheKey` carries both as bit
patterns. That is what this does — the key was widened in the same commit as the property, which is what row
2g asked for.

### Numbers

| case | before | after |
| --- | --- | --- |
| `"abc"`, `letter-spacing: 2px` | layout +6 px, pen +0 | both +6 px (3 glyphs) |
| `"a b"`, `word-spacing: 5px` | layout +5 px, pen +0 | both +5 px, at the space |
| fragment width vs shaped run | box wider than the ink | equal within 0.01 px |
| shaped-run cache | one entry per (font, size, text) — a spaced request could be served an unspaced run | one entry per (font, size, letter bits, word bits) |

Rendered: 32 px text, `letter-spacing: 8px`, centred in a 400 px page — ink 79-309 (231 px), centre 194. The
6 px offset from the page centre is the trailing letter-spacing: the box carries a gap after the last glyph
that has no ink, exactly as upstream notes it does ("the spec says that we 'really should not' apply
letter-spacing to the trailing edge of a line but other browsers do so we will as well").

Suite **65/65 twice**, 129.99 s and 135.76 s.

### What I got wrong

| First reading | What was true | What settled it |
| --- | --- | --- |
| "`css_length_px(value, 0, size)` reads the fallback as the size" | its signature is `(value, reference, fallback)` — I passed them swapped, so a *missing* `letter-spacing` became 16 px | the 2h assertion failed: `measure_text("To")` no longer matched the shaped width |
| "the build is clean" | `ninja` had not run: the pip-installed `ninja` vanished with the previous environment, and a `grep` over its empty output read as success | `ninja -n` printing nothing and `ninja: command not found` in the next call |

That second one is the hazard in row 4d wearing a different hat: an absent tool and a clean build look
identical when the output is filtered.

### What this row would do differently

1. **Read the signature before calling it.** `css_length_px` has three parameters with defaults, and a swapped
   pair compiles silently.
2. **Check that the build tool exists before trusting a build.** `command -v ninja` is one word.

---

## 10. Fifth commit — rows 2b and 2c: status drift, not new work

Both were ticked `[ ]` and both were done in code. The checklist's own rule is "if a row's status is wrong,
fix it in the next commit and say so in the message", so this commit ticks them and adds the proofs that were
missing, rather than claiming anything new was built.

| row | what was already true | what this commit adds |
| --- | --- | --- |
| 2b | `draw_text` rasterises through `cached_glyph()` → `rasterize_glyph()` | `AetherisGlyphRasterizer` asserts the cached bitmap equals the rasterised one, value for value ('A' at 32 px: 24x26, 221 inked, 115 partial-coverage pixels) |
| 2c | GPOS kerns a shaped run; the embedded pair table kerns the fallback (A+T = -159) | `AetherisShaping` asserts both, and that the table answers only for the active font |

### What the proofs cost

| first attempt | what was true | what settled it |
| --- | --- | --- |
| `kerning_adjustment(font, 'T', 'o')` on a local `TtfFont` | returns 0 for every pair unless the font *is* `active_font()` — the table holds the embedded subset's glyph ids | a probe over all ASCII pairs printed zero pairs, then 12 the moment it asked the active font |
| `T + o` as the pair | not in the table; `A + T` is | the probe |

Two more rows' worth of the same lesson as 2h and 2g: the fixture, not the feature, is what fails first.

### One thing found and not fixed

`cached_glyph()` keys on `(glyph, size)` and not on the font. That is correct while `active_font()` is a
singleton — the only font paint can reach — but it would serve the wrong bitmap the day a second font exists
(`@font-face`, row 3d). Recorded here rather than in a row, because row 3d is where it becomes reachable.

---

## 11. Sixth commit — row 3a6: `content` as a list, with alt text

| what | where |
| --- | --- |
| `ContentValue`: kind (Normal/None/List), `items`, `alt_text`, `unsupported_items`, `text()`, `alt()` | `src/rendering/content_value.{hpp,cpp}` |
| The parser quotes **each** string item instead of wrapping the whole value, so item boundaries survive into the declaration string | `src/rendering/css_parser.cpp` (`quote_string_items`) |
| `make_generated_box` asks `ContentValue` instead of stripping quotes by hand; the alt text lands on `LayoutNode::generated_alt_text` | `src/rendering/layout.{hpp,cpp}` |
| Parsing assertions: two items, alt text, `none`, `normal`, empty item, unsupported item | `Tests/AetherisRendering/TestCss.cpp` |

Upstream's shape: `ContentStyleValue` holds a `StyleValueList` plus an optional alt-text list
(`LibWeb/CSS/StyleValues/ContentStyleValue.h:16-45`), and the tree builder asks it for Normal, None or List
(`Rust/src/layout/tree_builder.rs:1714`, `FfiComputedContentType`). Same three kinds here.

### What changed in behaviour

| declaration | before | after |
| --- | --- | --- |
| `content: 'a' 'b'` | text "a b", two fragments (the space came from the whitespace token) | **"ab"**, one fragment |
| `content: 'a b'` | "a b", two fragments | unchanged |
| `content: 'C' / 'alt text'` | text "'C' / 'alt text'" — the slash rendered on the page | text "C", alt "alt text" kept on the node |
| `content: ''` | empty box | empty box, unchanged (row 3a4 holds) |
| `content: attr(title)` | rendered the literal text "attr(title)" | renders nothing, and counts the item as unsupported |

Verified end to end with a probe that walks the layout tree: `[ab]`, `[a b]`, `[C] alt=[alt text]`, and an
empty box with no fragments.

### What is still not there

Nothing reads `generated_alt_text` yet — there is no accessibility tree, and that is tier 4. It is on the node
so the parser's work is not thrown away between here and there. `attr()`, `counter()`, `open-quote` and the
`/`-less forms of those are counted in `unsupported_items` rather than implemented.

### What this row would do differently

1. **Trim after quoting, not before.** The first version wrapped the trimmed value but then built the quoted
   string from the untrimmed tokens, so `content: 'C'` came back as ` 'C'` and the existing CSS test failed on
   a leading space.
2. **A pure function is a cheap test.** `parse_content_value` took six assertions and no font, no document and
   no pixels; the layout wiring needed one probe to confirm, and both found real mistakes.

---

## 12. Seventh commit — row 3i: the divergence was not there

The row said: "at an exactly-full line this wraps anyway; upstream overflows. Ours never overflows — the safer
error, recorded not hidden." Measured with a probe, both halves are wrong.

| case | measured | spec / upstream |
| --- | --- | --- |
| `"aaa"` in a 25 px box | one fragment, 30 px wide — overflows | overflows |
| `"aaaaaaaa"` (80 px) in a 30 px box | one fragment, 80 px wide | overflows |
| `"aaa bbb"` in a 30 px box | two lines, one per word | wraps at the space |
| two 60 px `inline-block`s in 100 px, **no whitespace** | both at `y=0`; the second at `x=60`, overflowing | overflows |
| the same two with a space between | the second at `y=10` | wraps |

So there was nothing to fix. The row is marked `[-]` — out of scope because the gap does not exist — and the
five cases are now assertions in `AetherisTextLayout`, which is the part that has value: a future change to the
wrap pass cannot quietly start wrapping where it should overflow.

### What this row would do differently

Nothing about the code. About the process: a row written from a *symptom* can name a fault that is not there,
and this one survived two sessions because nobody measured it. The same probe that closed 2h, 2g and 2b would
have closed this in ten minutes on the day it was written.

---

## 13. Eighth commit — row 3b: `position: sticky`

| what | where |
| --- | --- |
| `StickyRegion`: the box's command range, its geometry, its containing block's bounds, and `top`/`bottom` | `src/rendering/render_tree.hpp` |
| `emit` records one per sticky box, after its subtree is emitted | `src/rendering/render_tree.cpp` |
| `PaintExecutor` shifts those commands at paint time: `clamp(scroll + top - node_top, 0, container_bottom - node_bottom)` | `src/rendering/paint_executor.cpp` |
| Sticky children sort after in-flow siblings, because sticky creates a stacking context | `render_tree.cpp` |
| Four assertions across four scroll offsets | `Tests/AetherisRendering/TestTextLayout.cpp` |

Why paint time and not layout: the scroll path is documented as repaint-only ("the document is not re-laid out:
scrolling repaints, it does not reflow"), so a sticky offset computed during layout would be stale the moment
the page scrolled. The render tree therefore carries the geometry and the paint pass carries the scroll offset.

### Numbers

200x120 viewport, sticky box in a container spanning document rows 60-160, `top: 10px`, magenta so the first
magenta row is the box:

| scroll | box at surface y | why |
| --- | --- | --- |
| 0 | 60 | still in its natural place |
| 60 | 10 | stuck 10 px below the scrollport top |
| 100 | 10 | still stuck |
| 140 | 0 | the container's bottom has arrived; the shift is clamped |

### What went wrong, and what it cost

The first version shifted the box but painted it **underneath** the sibling that follows it in the document, so
at scroll 120 the magenta was there and invisible. `position: sticky` creates a stacking context; the fix is to
sort sticky children after the in-flow ones (before the z-index comparison), which is three lines in the child
ordering. The probe found it by reporting "no magenta" at one scroll offset and "correct" at the others.

### Still not done, and named

`bottom` is read and applied symmetrically, but only `top` is exercised by a test — a page that sticks to the
bottom edge is untested. Absolute and fixed children still paint in document order rather than after in-flow
content, which is the same class of bug this row hit; changing it would move pixels this row is not about, so it
is recorded rather than fixed.

---

## 14. Ninth commit — row 3a3: the generated box paints, both halves of it

The row said "needs a synthetic text node whose `dom_node` is null, which several paths dereference". Measured,
the text already painted — `::before { content: 'X'; color: #ff00ff }` puts 31 magenta pixels on the page and 109
at 28 px. What did *not* paint was the box.

| what | before | after |
| --- | --- | --- |
| generated box, text only | painted | unchanged |
| generated box, background + text | **background missing** | background, then the glyph over it |
| generated box, border + no text | painted | unchanged |
| generated `inline-block` with text, no width | **200 px wide — the whole line** | 11 px, the same as a real span holding "X" |

Two causes, both one line's worth:

1. In `RenderTree::emit` the background/border/shadow/outline commands were the `else` of `if
   (!node.text_fragments.empty())`. A normal element never has text fragments of its own (its text is a child
   text node), so the bug only showed on the one node kind that carries both: a generated box. They are now a
   `paint_box` lambda called before the text, which is also the CSS painting order.
2. In `LayoutEngine::layout_node` the text branch never set `box.content.width`, so a generated box kept the
   whole available width. A normal inline-block gets its width from the parent's shrink-to-fit pass, which
   measures the child — and a generated node has no DOM node for that pass to measure. It now shrinks to the
   text's natural width, never growing past a specified one.

### What this row would do differently

1. **"It does not paint" is three claims.** Text, box and sizing are separate; the probe that counted magenta
   pixels per declaration combination separated them in ten minutes, and the row had conflated them for days.
2. **Relink the probe.** The first run after the fix still showed the old numbers because the probe is statically
   linked against `libAetherisRendering.a` — the library had been rebuilt, the probe had not. Same family as the
   stale-binary trap in the 11-18 handover, one step further down the chain.

---

## 15. Tenth commit — row 3c1: flex completeness

Row 3c was "complete flex and grid", which is two algorithms in one row. Split: **3c1** flex (this commit),
**3c2** grid areas and named lines (still open).

| what | before | after |
| --- | --- | --- |
| `flex: 1` | **ignored** — the shorthand was never parsed, so the commonest flex declaration in the wild did nothing | `1 1 0%`, as css-flexbox-1 §7.1 defines it |
| `flex-basis` | ignored | the base size, ahead of `width` |
| overflow in a row | `free_space` was clamped at 0, so items overflowed the container | items shrink in proportion to `flex-shrink × base`; `flex-shrink: 0` refuses |

Upstream: `Libraries/LibWeb/Rust/src/layout/flex_formatting_context.rs:797-820` (`used_flex_basis_for_item`:
`content`, then `auto` → the main size property → `content`, then a length or percentage) and `:1022`
(`determine_flex_base_size`). The distribution here is the simple form of §9.7 — no min/max clamping beyond zero,
because the engine has no intrinsic min-content pass for a block child.

### Numbers, 300 px row, three items

| declaration | widths |
| --- | --- |
| `.i0,.i1,.i2 { flex: 1 }` | 100 100 100 |
| `.i0 { width: 100px } .i1,.i2 { flex: 1 }` | 100 100 100 |
| `.i0 { flex: 2 } .i1 { flex: 1 } .i2 { flex: 0 0 0px }` | 200 100 0 |
| `.i0,.i1 { flex-basis: 200px }` | 150 150 0 |
| `.i0,.i1 { flex-basis: 200px; flex-shrink: 0 }` | 200 200 0 (overflows) |
| `.i0,.i1,.i2 { flex: 0 0 150px }` | 150 150 150 |

`AetherisLayout` pins all six. Suite **65/65 twice**, 135.15 s and 141.02 s.

### What is still missing from flex, and named

`flex-basis: auto` on an item with no `width` falls back to zero rather than to its content, because there is no
intrinsic-width pass for a block child. `flex-wrap`, `align-content` and `order` are unread. Margins on flex items
are not subtracted from the free space.

### The environment trap this commit walked into

This session's `/scratch/work` was restored from a snapshot **older than the pushed history**: `HEAD` was at
`5a8a5d7a` (row 2f) while `origin/main` was at `5d47ff44` (row 3a3), and the working tree matched the *older*
state. The flex work was therefore committed on the wrong base, in a commit that would have reverted rows 3a6, 3i,
3b and 3a3 if it had been pushed.

What caught it: `git push` was rejected as a non-fast-forward, and `git diff --stat origin/main HEAD~1` showed 25
files and 909 deletions — a "single feature" commit that deletes 900 lines is not a single feature.

What fixed it: the commit was preserved on a branch (`flex-work-preserved`), the branch reset to `origin/main`, and
only the flex hunks re-applied — `style.cpp` (pure addition), `layout.cpp` (the flex function only), `TestLayout.cpp`
(additive), and the three doc edits. Then rebuilt and re-ran the suite, because the source files had changed
underneath the build tree.

**Rule for the next session: compare `git log --oneline -1` against `git fetch && git log --oneline -1
origin/main` before the first commit, not after the first rejected push.**

---

## 16. Eleventh commit — row 3c2: grid placement

| what | before | after |
| --- | --- | --- |
| `grid-template-areas` | not parsed at all | a name grid; `.` is an empty cell |
| `grid-area: <name>` | ignored — the item landed wherever document order put it | the name's bounding box, spans included |
| `grid-column` / `grid-row` | ignored | a 1-based line, or `start / end` |
| auto-placed items | index ÷ columns | the first free cell, so an explicit item does not shove them |

Upstream: `Libraries/LibWeb/Rust/src/layout/grid_formatting_context.rs:746` (`find_unoccupied_grid_area`) — an
occupation grid scanned along the auto-flow axis. This is that shape with one axis: row-major, single-cell items
auto-placed, spans only for explicit placement.

### Numbers, 300 px container, three 100 px columns

| case | result |
| --- | --- |
| `grid-template-areas: 'h h h' 'l . r'`, `grid-area: h` | x=0, y=0, **w=300** |
| the same, `grid-area: l` / `r` | x=0 y=20 / x=200 y=20 — the `.` cell is skipped |
| `grid-column: 2 / 4` | x=100, **w=200** |
| explicit `grid-column: 2`, then two auto items | x=100, x=0, x=200 |
| plain auto row-major (control) | 0, 100, 200, then the next row at y=20 — unchanged |

`AetherisLayout` pins all of them. Suite **65/65 twice**, 154.66 s and 159.66 s.

### The bug that hid the whole feature

The first version placed nothing: the tokenizer strips quotes from string tokens, so `grid-template-areas: 'h h h'`
arrived as `h h h` and the parser found no rows. The parser now re-quotes `grid-template-areas` the same way it
re-quotes `content` (3a6) — and then the areas parser had to accept **single** quotes, because that is what the
re-quoting writes. Two faults, both invisible from the outside: the grid looked like it ignored areas.

### Still not read

Named lines (`[name]`), `span n`, `auto-fit`/`minmax`, `grid-auto-flow: column`, and `grid-template-rows` only
understands pixel tracks (an `fr` row falls back to the tallest item).

---

## 17. Twelfth commit — row 3d split, and the first half of 3d1

Row 3d was "`@font-face`, transitions, animations": three features that share nothing but a row. Split into
**3d1** `@font-face`, **3d2** transitions, **3d3** animations, and this commit does the part of 3d1 that stands on
its own.

| what | before | after |
| --- | --- | --- |
| `@font-face { ... }` | skipped with every other unknown at-rule | read into `CssStyleSheet::font_faces` as family + the url inside `src` |
| `cached_glyph()` key | `(glyph, size)` | `(font identity, glyph, size)` |

The cache key is groundwork, not a bug fix for today: with one active font the identity never varied. It becomes a
correctness requirement the moment a second font exists, and the pattern is the one row 2g asked for — widen the
key before the feature that needs it, not after.

### Evidence

| assertion | where |
| --- | --- |
| a quoted family keeps its name, `url(...)` yields the source, a block with no family is dropped, the rule after it still parses | `AetherisCss` |
| the same glyph id from two fonts is two cache entries — the second font is the embedded one with a padding byte flipped, so the shapes are identical and only the key tells them apart | `AetherisGlyphRasterizer` |

Suite **65/65 twice**, 145.21 s and 137.77 s.

### What 3d1 still needs, precisely

1. **Fetch the source.** The resource loader has no `data:` URL support, and nothing calls it for `@font-face`.
2. **Select the face.** `font-family` is inherited and never read for selection; `Font`, `FontMetrics`, `measure()`
   and `draw_text()` all go through `active_font()`, which is a singleton. Each would need to carry a face.

That is a second commit's worth of plumbing across five files, and it is written down rather than estimated away.

### What this commit would do differently

Nothing about the code. About the row: "three features" was never one row, and saying so cost nothing once the
measurement (what does the parser do with `@font-face`? — nothing) was in hand.

---

## 18. Thirteenth commit — row 3d1: `@font-face` end to end, for local files

| what | before | after |
| --- | --- | --- |
| `@font-face` | skipped with the other unknown at-rules | parsed into `CssStyleSheet::font_faces` (previous commit) |
| a declared face | nothing loaded it | `RenderDocument` reads a `file://` or plain-path `src` and registers it |
| `font-family` | inherited and never read | `font_for_family()` resolves the list to the first face that exists, else the engine's own font |
| `Font`, `FontMetrics` | every path called `active_font()` | carry the resolved face |
| the pen | `draw_text` called `active_font()` | receives the face paint resolved from the command's family |

That is the plumbing the previous commit named as "five files' worth": `font_library`, `font`, `render_tree`,
`paint_executor`, `software_surface`, `render_document`.

### Evidence

| assertion | where |
| --- | --- |
| the embedded bytes register as a face; the family finds it; an unknown family has none; a comma list falls through to the one that exists | `AetherisShaping` |
| `Font::from_style` carries the named face, and an unknown family falls back to `active_font()` | same |
| building a document with `@font-face { src: url(/tmp/…ttf) }` registers the face and puts the family on the text command | same |

Suite **65/65 twice**, 141.13 s and 135.05 s.

### Not done, and in the row

An `http(s)` or `data:` source is not fetched — the family then falls back to the engine's font, which is the same
thing a browser does for a face that fails to load. `font-weight` and `font-style` do not select among faces of one
family: the registry is keyed on the family name alone.

### What this row would do differently

The two halves were the right split, and the second half was four compile errors in a row (a member reference, two
missing includes, an undeclared function) — all mechanical, none a design problem. The lesson is the boring one:
add the include when you add the use, and read the error's first line rather than the wall of compiler flags.

---

## 19. Fourteenth commit — row 3d2 started: the curve and the arithmetic

A transition is a curve plus a clock. This commit is the curve and the arithmetic, both of which can be checked
without a clock, a document or a pixel.

| what | where |
| --- | --- |
| `Easing`: the four named curves, `linear`, `cubic-bezier(a,b,c,d)`, `steps(n[, jump-start|jump-end])` | `src/rendering/easing.{hpp,cpp}` |
| the cubic-bezier solve | ported from `LibWeb/Rust/src/css/animation.rs:217-300`: Newton-Raphson for eight steps at 1e-7, bisection for sixty-four, and the tangent extension outside [0, 1] that css-easing-1 asks for |
| `Transition`: property, from, to, duration, delay, start time, easing, `value_at(now)`, `finished_at(now)` | `src/rendering/transition.{hpp,cpp}` |
| `transition_spec_from_style()`: the shorthand in any token order | same |
| 20 assertions: the curves at 0, 0.5 and 1; steps; a transition before, during and after its delay; a zero duration; the shorthand both ways round | `Tests/AetherisRendering/TestEasing.cpp` (new binary, so the suite is **66**) |

### Numbers

| curve | at 0.5 |
| --- | --- |
| `linear` | 0.5 |
| `ease` | **0.802403** (the standard value is 0.8024) |
| `ease-in` | below 0.5 |
| `ease-out` | above 0.5 |
| `ease-in-out` | 0.5 |
| `cubic-bezier(0, 0, 1, 1)` | 0.3 at input 0.3 — the solve agreeing with the identity |
| `steps(2)` at 0.4 / 0.6 | 0.0 / 0.5 |

Suite **66/66 twice**, 136.06 s and 146.45 s.

### What 3d2 still needs

1. **A clock.** `render_to_surface` is static: nothing advances time. The window loop has frames, so the clock
   belongs there and on `RenderDocument`, and paint reads it.
2. **The trigger.** A transition starts when a property's computed value changes. The style cache already notices
   which nodes are dirty; it does not record *which property* moved, which is what starting a transition needs.

### What this row would do differently

Nothing so far — this is the part of a feature that can be built and proven before the awkward part, and the row
says exactly where the awkward part starts.

---

## 20. Fifteenth commit — row 4d: the guard against trusting a stale binary

This row was a hazard, not a feature: twice today a build was killed by the sandbox's time limit, ninja printed
nothing anyone read, and the next `ctest` passed on the *previous* binary. A passing suite that tested last hour's
code is worse than a red one, because it is trusted.

`tools/check-build.py` asks the two questions that went unasked:

| question | what it catches |
| --- | --- |
| does ninja have pending steps? | an interrupted build |
| is any source newer than the binary? | a build that never ran |

It prints the newer files, exits 1 when stale, 2 when there is no build directory.

### Verified here, in an environment that could not compile anything

| case | result |
| --- | --- |
| this tree, with the 3d2 clock patch unbuilt | exit 1: **57 pending steps**, 2 newer sources named |
| `--` a build directory that does not exist | exit 2 |
| a binary the build never linked | exit 1, "has never linked it" |

Written as one process with no subprocesses for the source check, because the environment that needed it allows
exactly one level of process creation — `sh -c 'echo ok'` runs, `sh -c 'ls'` cannot fork, and `zig` cannot spawn its
own frontend. A guard that cannot run is not a guard. The ninja check is best-effort and says so when it cannot ask.

### What this row would do differently

Nothing. It is thirty lines, it was written and tested in the environment that motivated it, and its first run found
the exact condition it was written for.

### The row's own advice, applied

Run `python3 tools/check-build.py` before trusting a green suite. Its 57-pending-steps answer is the reason the 3d2
clock patch is parked rather than committed.

---

## 21. Sixteenth commit — row 3d2 closed: transitions run

The parked patch was verified first thing in a healthy environment, and the guard from the previous commit was used
to check the build before the suite was trusted — it reported 57 pending steps and stopped exactly the mistake it
was written for.

| part | where |
| --- | --- |
| the curve: `ease`, `linear`, `ease-in/out/in-out`, `cubic-bezier()`, `steps()`, ported from upstream's solve | `easing.cpp` |
| the arithmetic: `Transition::value_at(now)`, the `transition` shorthand in any order | `transition.cpp` |
| the clock: `RenderDocument::set_time()`, `start_opacity_transition()` | `render_document.cpp` |
| paint: `RenderTree::from_layout(root, opacity_overrides)`, and `render_to_surface` rebuilding the tree from the existing layout when a transition is live | `render_tree.cpp`, `render_document.cpp` |

### Numbers — 50x50 red box, 200x100 page, opacity 1 → 0, linear, 1 s

| t | pixel at (20, 20) | rect |
| --- | --- | --- |
| 0.00 | rgba(255, 0, 0) | x=10 y=10 50x50 |
| 0.25 | rgba(255, 64, 64) | unchanged |
| 0.50 | rgba(255, 128, 128) | unchanged |
| 0.75 | rgba(255, 192, 192) | unchanged |
| 1.00 | rgba(255, 255, 255) | unchanged |

`layout_passes()` 1 before, 1 after: **a transition repaints, it does not reflow.** Suite **66/66 twice**, 211.64 s
and 185.69 s.

### The guard earned its keep on its first day

`tools/check-build.py` reported `57 step(s) pending` when this session resumed, which is the state that twice made a
green suite meaningless. Two false positives were fixed while using it: `tools/*.py` counted as compiled sources, and
comparing against the *oldest* binary made a test-only edit report `aetheris` as stale. It now compares the newest
source against the newest binary, and checks ninja's pending steps as well.

### Still open in this row's subject

| gap | what it needs |
| --- | --- |
| the trigger | a style change starting a transition: the cache records which nodes are dirty, not which property moved |
| properties | only `opacity` is interpolated; colour, transform and lengths are next |
| `transition` shorthand | the first transition in a comma list is read, the rest are not |

---

## 22. Seventeenth commit — row 1g4c: `scrollRestoration`

Upstream first: the mode is a property of the **session history entry**, not of the document. `History.scrollRestoration`
reads and writes `active_session_history_entry()->scroll_restoration_mode()` (`HTML/History.cpp:229-242`). That shape
is what this ports — a mode per entry, so one page can ask to do its own scrolling while its neighbour does not.

| what | before | after |
| --- | --- | --- |
| history entries | `std::vector<std::string>` of URLs | `std::vector<HistoryEntry>`: url, scroll offset, mode |
| traversal | URL changed, scroll jumped to 0 | the destination entry's mode decides: `Auto` restores its offset, `Manual` leaves it to the page |
| the app | `viewport.scroll_to(0, 0)` after every load | `take_scroll_restore()` first, `(0, 0)` only when nothing was recorded |

API: `set_scroll_restoration()` / `scroll_restoration()` / `record_scroll(x, y)` / `take_scroll_restore(x, y)`. The
last answers **once**, so a repaint cannot re-apply an offset that has already been honoured.

### Evidence — `AetherisBrowserSession`

| assertion |
| --- |
| a new entry defaults to `Auto` |
| a forward navigation restores nothing (nothing was left behind) |
| back to an `Auto` entry restores **that entry's own** offset — 320, not the 40 the other page had |
| the restore is answered once |
| a `Manual` destination restores nothing |
| the mode is per entry: setting one to `Manual` leaves the other `Auto` |

Suite **66/66 twice**, 135.91 s and 131.35 s.

### Two corrections worth recording

| what I expected | what the spec says |
| --- | --- |
| setting `Manual` on the page you are *on* suppresses the restore when you leave | the **destination** entry's mode decides, so the test had to set it on the page being navigated *to* |

And a build-order fact: `viewport` was declared below the history hooks that now need it, so the declaration moved
above them. A lambda cannot capture what does not exist yet, and the compiler said so plainly.

### Still not done in this subject

`history.state` and `pushState` are row 1g4b, still open; `scrollRestoration` currently only has an API, not a
`window.history` binding — the JS side of it belongs with that row.

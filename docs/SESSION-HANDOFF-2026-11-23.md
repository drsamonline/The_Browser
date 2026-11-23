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

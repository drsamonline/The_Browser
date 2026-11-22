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

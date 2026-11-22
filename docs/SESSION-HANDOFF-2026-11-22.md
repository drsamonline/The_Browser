# Session handover — 2026-11-22

One row closed: **2d4, shaping cost**. Suite **64 tests, 64/64, twice**, 126.21 s and 129.14 s at
`ctest -j2`. Tip: the commit that carries this file, `perf(shaping): key the caches on a font identity computed once, not per run`
(a commit cannot contain its own sha, and amending to insert one only moves it).

The row was not about HarfBuzz being slow. It was about a hash we were taking 756,000 times per shaped
run to avoid taking it once.

---

## 1. What shipped

| what | where |
| --- | --- |
| `TtfFont::identity()`: a content hash of the font file, computed **once in `load()`** | `src/rendering/ttf_font.{hpp,cpp}` |
| `shape_run()` takes that identity, and keys both caches on it | `src/rendering/shaper.{hpp,cpp}` |
| A shaped-run cache keyed by (font identity, size bits, text), plus the `hb_font_t` cached with the size | `src/rendering/shaper.cpp` |
| Both production call sites pass the identity: `Font::measure` and the drawing pen | `font.cpp`, `software_surface.cpp` |
| `tools/shaping-bench.cpp` + `tools/run-shaping-bench.sh`: the measurement, kept | `tools/` |
| Four assertions on the cache contract: identity is content, a hit equals a miss, the size is in the key | `Tests/AetherisRendering/TestShaping.cpp` |
| Rows 2e (bidi), 2f (cluster breaking), 2g (key must widen with letter-spacing) added; 2d3's claim corrected | `docs/CHECKLIST.md` |

## 2. Numbers that were measured, not assumed

| measurement | before | after | how |
| --- | --- | --- | --- |
| one 24-char run, **-O2** | **1.2571 ms** | **0.0001 ms** cached, 0.0086 ms cold | `tools/run-shaping-bench.sh build-rel` |
| one 24-char run, **-O0** | **~34 ms** | 0.0027 ms cached, 0.1863 ms cold | `tools/run-shaping-bench.sh build` |
| HarfBuzz alone, face and font already built | 0.0046 ms (-O2), 0.1325 ms (-O0) | unchanged | same bench |
| FNV-1a over the 756 KB DejaVu file | **1.2532 ms (-O2), 30.65 ms (-O0)** — per call | once per load | same bench |
| `hb_font_create` per call | 0.0003 ms (-O2), 0.0058 ms (-O0) | now once per (font, size) | same bench |
| the suite, `ctest -j2` | 210.28 s | **126.21 s, 129.14 s** | `ctest --test-dir build -j 2` |
| `measure("The quick brown fox jump")` at 16 px | 207.1406 px | 207.1406 px, unchanged | bench |

The 1.043 ms figure in the 11-18 handover was taken **before** the face cache existed (`18eb91c8`).
The cache did not remove 0.075 ms of work; it added a 1.23 ms hash per call at -O2 and a 30 ms one at
-O0, and the note that recorded it priced the face creation it saved instead.

## 3. The errors, and what each cost

| first reading | what was true | what settled it |
| --- | --- | --- |
| "`Font::measure` is faster than `shape_run`, so the shaper is not in the text path" | the bench never set `AETHERIS_FONT`, so `measure` took the bitmap fallback — the same trap the 11-18 handover names, walked into again | setting the variable: measure went 0.0446 -> 1.4292 ms |
| "the font hash costs nothing: 0.0000 ms" | the loop was dead code, its result unread, deleted by the compiler | a `volatile` sink |
| "the release build is slower than the debug one (1.268 vs 1.043 ms)" | the -O0 figure predates the hash entirely | the breakdown: the hash is 98% of the -O2 run and 90% of the -O0 one |
| "the -O0 bench hangs at 1 iteration" | one call cost 31 ms at -O0, and the bench made ~150 of them | a probe that stamps each step, unbuffered |
| "key the cache on a hash memoised by buffer address" (considered, not written) | that is the bug `18eb91c8` removed: an address is reused when a font reloads into the same buffer | reading the 2d3 rationale before writing the code |

Five readings, four of them corrected by a measurement. The fifth was caught by reading the comment
above the code being changed.

## 4. Build facts a new session needs

| fact | consequence |
| --- | --- |
| `cmake` configure on a **fresh** build dir exceeds the 300 s cap on the first attempt | run it a second time: 151.7 s (Debug) and 105.1 s (Release) once the compiler probes are warm |
| `ninja -C build-rel AetherisRendering` | 137 targets instead of 283: enough for the bench, and it links a measurable release library in four calls |
| the bench must be compiled at the same `-O` as the library it links | at -O0 the bench's own hash loop is ~20x the library's copy, which prices the compiler, not the engine |
| `write_file` leaves root-owned files in this sandbox | `chmod` fails on them: `cp` out, `rm`, `cp` back to own the file, then `chmod +x` |
| the Debug `libharfbuzz.so` shapes at 0.13 ms per 24 chars against 0.0033 ms in Release | never quote an -O0 shaping number as the engine's cost |

## 5. Open work, in the order I would take it

| row | item | why here |
| --- | --- | --- |
| 2e | bidi: split runs by direction before shaping | HarfBuzz does none; RTL inside LTR is shaped in the wrong order, and now there is a cache that would serve the wrong run for a mirrored string |
| 2f | line breaking by cluster (`HB_GLYPH_FLAG_UNSAFE_TO_BREAK`) | the break pass still uses whitespace |
| 3a6 | `content` as a real list type, with alt text | parser quoting is still a stand-in |
| 1g4b | `pushState`/`replaceState`/`history.state` | needs the JS value serialisation decision first |
| 3i | overflow at an exactly-full line | recorded divergence |
| 3c, 3d | flex/grid completeness, `@font-face`, transitions | large |
| 0.1 | tier-0 coverage corpus | **blocked**: no open internet |

## 6. What this session would do differently

1. **Price the key before adding the cache.** `18eb91c8` added a full-file hash to save a face
   creation, and the note recorded the saving, not the cost. A cache whose key is expensive is a
   pessimisation with a plausible story.
2. **Measure a cache twice: hit and miss.** One number cannot tell you which of the two you built.
3. **Compile the harness at the same optimisation as the thing it measures.** Half of the first
   afternoon's confusion was a bench priced at -O2 against a library built at -O0.
4. **Set the precondition in the harness, not in the shell.** `AETHERIS_FONT` in the bench, not in the
   command line, so the next person cannot run it without one.

---

# Second batch, same day — row 2e: bidi level 1

Suite **65 tests, 65/65, twice**, 125.14 s and 125.28 s at `ctest -j2` (one new test, `AetherisBidi`).

## 7. What shipped

| what | where |
| --- | --- |
| `bidi_class()`: a range table for R/AL/number, everything else LTR | `src/rendering/bidi.{hpp,cpp}` |
| `split_directional_runs()`: cut at the direction change, neutrals by N1/N2, numbers in their own LTR run | same |
| `may_require_bidi()`: upstream's gate, asking the table and not the byte value | same |
| `shape_run()` shapes one buffer per run with `hb_buffer_set_direction()` forced, clusters still byte offsets into the whole text | `shaper.cpp` |
| `TestAetherisBidi`: nine split cases, the gate, cluster order inside the RTL run | `Tests/AetherisRendering/TestBidi.cpp` |

Upstream read first: `LibUnicode/Bidi.cpp` (`may_require_bidi_processing()`), `LibWeb/Layout/TextNode.cpp:556-620`
(`text_type_for_code_point()`, the 128-entry ASCII table plus `bidirectional_class()` mapped to
`GlyphRun::TextType`), `LibGfx/TextLayout.cpp:169-190` (`setup_text_shaping` sets the direction from the
run's text type before `guess_segment_properties`). The reordering itself lives in the Rust layout, which
is not vendored here — which is why this row is level 1 and 2e2 exists.

## 8. Numbers that were measured

| measurement | value |
| --- | --- |
| "abc سلام xyz" through one guessed buffer | 12 glyphs, Arabic in isolated forms, clusters ascending 0-15 |
| the same string split into ltr / rtl / ltr | **11 glyphs**, RTL run's clusters descending 10, 6, 4 |
| "سلام عليكم" split | one RTL run, clusters 17 15 13 11 9 8 6 2 0 — nine glyphs, the lam-alef ligature formed |
| the gate's cost on ASCII | `shape_run` cold 0.0086 -> 0.0058 ms at -O2; a byte test per character and nothing else |
| rendered "سلام عليكم", ink groups | 30 px on the left, 37 px on the right: the **first** logical word is on the left |
| rendered "عليكم سلام" | 37 px on the left, 30 px on the right: mirrored, as a logical-order paint of swapped words would be |

That last pair is the row's real finding. `TextLayout::layout` (`text_layout.cpp:20-22`) tokenises a text
node into **one fragment per word**, and `RenderTree::emit` paints one `DrawText` command per fragment. So
the shaper now gets each word right and the *line* still reads in logical order: the fix for the line is
UAX#9 level 2, which needs the line's fragments and a paragraph direction, not a shaper change. Row 2e2.

## 9. The errors, and what each cost

| first reading | what was true | what settled it |
| --- | --- | --- |
| "any byte above 0x7F needs bidi processing" | "café" and "١٢٣" are non-ASCII and need none | the test failed on `café`; the gate now asks `bidi_class()` |
| "a neutral takes the direction of the text before it" | that is N1's same-direction case only; N2 gives the paragraph direction, so the space in "abc سلام xyz" belongs to the Latin side | the failing split assertion: `rtl[سلام ]` instead of `rtl[سلام] ltr[ xyz]` |
| "the render is wrong: سلام is on the left of سلام عليكم" | the render is right for a *shaper* and wrong for a *line*: the words are separate fragments | measuring the ink groups of each word alone, then of the phrase |
| "`--render file:///...` will show me the page" | the CLI registers only the HTTP backend, so a local file has no backend at all | `Fetch failed: Resource is not cached and no backend supports the URL`; a 40-line harness against `RenderDocument::create()` instead |

## 10. Open work, in the order I would take it

| row | item | why here |
| --- | --- | --- |
| 2e2 | reorder a line's runs into visual order (UAX#9 L2) | the visible half of bidi; needs a paragraph direction, per-fragment levels and a reorder step in `TextLayout::layout` |
| 2f | line breaking by cluster (`HB_GLYPH_FLAG_UNSAFE_TO_BREAK`) | the break pass still splits on whitespace |
| 3a6 | `content` as a real list type, with alt text | unchanged |
| 1g4b | `pushState`/`replaceState`/`history.state` | needs the JS value serialisation decision first |
| 2g | widen the shaped-run key before letter-spacing | the key has no spacing bits |

## 11. What this batch would do differently

1. **Ask which layer owns the symptom before writing the fix.** "Arabic renders in the wrong order" was
   two faults in two layers: the shaper had no direction, and the line had no reordering. The row named
   the first; the render showed both.
2. **A pixel measurement needs a reference.** Ink widths of each word rendered alone turned an ambiguous
   screenshot into a number that named the word on the left.
3. **Check the CLI can see the input before blaming the engine.** `file://` was never registered.

---

# Third batch, same day — row 2e2: bidi level 2

Suite **65 tests, 65/65, twice**, 125.97 s and 130.42 s at `ctest -j2`. No new test binary: the level-2
cases went into `AetherisBidi`, which is where the level-1 cases already were.

## 12. What shipped

| what | where |
| --- | --- |
| `visual_order()`: levels 0/1/2 per fragment, N1/N2 for neutrals, numbers level 2 inside RTL, then UAX#9 L2 | `src/rendering/bidi.cpp` |
| `TextLayout::layout` rewritten from one minified line into a readable function that places fragments in visual order; LTR behaviour identical | `src/rendering/text_layout.cpp` |
| `direction` added to the inherited properties, so a text node sees its block's `direction: rtl` | `src/rendering/style.cpp` |
| Six order assertions plus the fragment order a line returns | `Tests/AetherisRendering/TestBidi.cpp` |

## 13. Numbers that were measured

| case | before | after |
| --- | --- | --- |
| "سلام عليكم" (Latin paragraph) | ink groups 30 px left, 37 px right — first word on the left | **37 px left, 30 px right** — first word on the right |
| "عليكم سلام" | same as above (the two were indistinguishable) | the mirror of it |
| "abc سلام xyz", `direction: rtl` | identical to the LTR paragraph | "xyz سلام abc" — the Latin ends swap out |
| "سلام 123", `direction: rtl` | "سلام 123" | "123 سلام", digits in their own order |
| Latin line in an RTL paragraph | — | unchanged: reversed twice by L2 and back |

## 14. The errors, and what each cost

| first reading | what was true | what settled it |
| --- | --- | --- |
| "N2 means the neutral is left to right" | N2 means the *paragraph* direction; in an RTL paragraph a neutral is RTL | the test failed with both spaces at the ends of the line: `|xyz|سلام|abc| ` |
| "`direction: rtl` does not work" | it was never reaching the text node: `direction` is inherited in CSS and was missing from the inheritance list | dumping every node's resolved `direction` showed `-` everywhere |
| "the page's `<style>` is applied by `RenderDocument::create()`" | it takes the author CSS as an argument; only the browser path collects `<style>` (`src/main.cpp:1858`) | the render came out at 16 px when the page asked for 28 px |
| "the class selector is broken" | it was fine; the sheet was never passed | passing the CSS explicitly made `.rtl` work |

Three of those four were the harness, not the engine. The habit that caught them: print the resolved
value rather than infer it from the picture.

## 15. Open work, in the order I would take it

| row | item | why here |
| --- | --- | --- |
| 2h | layout measures per code point, paint draws shaped glyphs | the fragment box is the pre-shaping width, so Arabic text is placed by one metric and drawn by another (سلام: 60 px against 57.5 px) |
| 2e3 | `text-align: start`/`end` follow `direction` | they are treated as left/right, which is wrong in an RTL block |
| 2f | line breaking by cluster | unchanged |
| 3a6 | `content` as a real list type, with alt text | unchanged |
| 1g4b | `pushState`/`replaceState`/`history.state` | unchanged |

## 16. What this batch would do differently

1. **Check the harness passes the input the page assumes.** Two hours of "the engine ignores `direction`"
   was the harness passing an empty stylesheet.
2. **Write the assertion before the render.** The order assertions named the failure in one line; the
   pixel comparison needed a reference render per word to say anything.
3. **A minified function is a wall, not a shortcut.** `TextLayout::layout` was one line of 1,900
   characters; rewriting it readably was most of the diff and none of the risk.

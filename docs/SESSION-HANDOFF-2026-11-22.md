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

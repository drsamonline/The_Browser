# Session handover — 2026-11-18

78 commits. Tip `0f7ca488`. Suite **64 tests, 64/64, twice**, 151-157 s at `-j2`.

This is the second handover for this date; the earlier one covered batches A and B of the selector work. This one
covers what came after: pseudo-elements, `content`, fragment navigation, shaping, and the test split.

---

## 1. What shipped, in order

| commit | what |
| `e62c9529` | a generated `::after` box flows inline, so it shares the line instead of starting a new one |
| `f8ddd0f9` | `content: ''` generates an empty box; `LayoutNode::is_generated` exists because `generated_text.empty()` is false for exactly that case |
| `760b882c` | a fragment change does not re-fetch: `navigate_in_place`, and a scroll to the indicated part |
| `8d567d82` | `tools/fetch-harfbuzz.sh` + the measurement that chose upstream CMake over the amalgamation |
| `16af7373` | `tools/run-shaping-smoke.sh`: `fi` is 1 glyph with `liga` on, 2 with it off, Arabic 4 code points give 3 glyphs |
| `15c6496e` | `shape_run()`, `TtfFont::data()`, HarfBuzz as an optional CMake target, `AetherisShaping` in the suite |
| `6d8ca43c` | `measure` and the drawing pen both use shaped advances |
| `18eb91c8` | the face cache, keyed by a hash of the font data |
| `60dfd68b` | the window test split into four, running at `-j2` |

## 2. Numbers that were measured, not assumed

| measurement | value | how |
| --- | --- | --- |
| HarfBuzz build | 70 TUs, `-O0 -j2`, three resumable commands | `ninja -C build harfbuzz`, killed at 280 s each time, resumed |
| `libharfbuzz.a` | 131 MB Debug, `hb_shape` present | `nm` |
| shaping correctness | `fi` 1 glyph with `liga` on, **2 with it off**, Arabic 4 cps -> 3 glyphs | `tools/run-shaping-smoke.sh` |
| engine width with the ligature | `measure("fi")` 10.0938 -> **10.0781** | `AetherisShaping` |
| shaping cost | **1.043 ms** per 24-char run at `-O0`; face creation is 0.075 ms of it | a 5000-iteration benchmark |
| cores on this box | **2** | `nproc` |
| window tests after the split | Core 47 s, Style 60 s, Layout and Geometry two at a time | `ctest -R Window -j2` |

## 3. The errors, and what each cost

| first reading | what was true | what settled it |
| --- | --- | --- |
| "`::after` does not paint" | it painted on the next line; a generated box took the block path | `AETHERIS_TRACE_GENERATED` |
| "`content: ''` needs quotes kept and nothing else" | the box was invisible: `generated_text.empty()` was the marker | a pixel that stayed white |
| "the hash setter does nothing" | the probe ran against a binary **73 minutes old** | the binary's mtime |
| "`location.hash` does not read back synchronously" | it does; the assertion was right the first time | a current binary |
| "the build succeeded" (three times) | `cmake --build \| grep error:` printed nothing when the build was killed at 300 s | the binary's mtime |
| "the four-way split has a hidden dependency" | **2 cores**; `-j4` ran eight processes on two | `nproc`, then `ctest -j2` |
| "the suite got slower after shaping" | noise; it swings 111-143 s on two cores | a direct benchmark |
| "the shaper does not shape" | the test never loaded a font (`active_font()` is loaded once from `AETHERIS_FONT`) | the numbers: 10.0781 vs 10.0938 |

**Eight corrections, every one a measurement away from the truth, every wrong reading plausible enough to act on.**
The single most expensive habit: trusting the absence of an error message instead of checking an artefact.

## 4. Build facts a new session needs

- Toolchain does not survive a pod: `pip install cmake ninja ziglang`, then rewrite `toolchain/zigcc` and `zigcxx` as
  `exec .../ziglang/zig cc "$@"`.
- HarfBuzz is **fetched, not committed**: `sh tools/fetch-harfbuzz.sh`. It writes its work directory next to the repo
  because `/tmp` is a 64 MB tmpfs and the sdist is 38.9 MB.
- The engine compiles with `-Werror -Wcast-qual` and `-fstrict-flex-arrays=2`. The vendored HarfBuzz target needs
  `-w -fstrict-flex-arrays=0`; without the second, HarfBuzz **panics on a valid font** because its OpenType tables end
  in `[1]` trailing arrays. Clang rejects the GCC spelling `-fno-strict-flex-arrays`.
- `ctest -j2`, never `-j4`: two cores.
- A killed build prints no error. Check `ls -la build/bin/aetheris`.

## 5. Open work, in the order I would take it

| row | item | why here |
| --- | --- | --- |
| 2d4 | measure shaping cost in a release build | 1.043 ms per run at `-O0` may be mostly the build type; measure before optimising |
| 3a6 | `content` as a real list type with alt text | the parser's quoting is a stand-in; `content: "x" / "alt"` cannot be expressed |
| 1g4b | `pushState`/`replaceState`/`history.state` | needs a decision on JS value serialisation first |
| 3i | overflow instead of wrapping at an exactly-full line | recorded divergence, small |
| 3c, 3d | flex/grid completeness, `@font-face`, transitions | large; one at a time |
| 0.1 | tier-0 coverage corpus | **blocked**: no open internet |

Not open, but worth knowing: bidi is still absent. HarfBuzz does none, so runs must be split by direction, and the
line-break pass still uses whitespace rather than `HB_GLYPH_FLAG_UNSAFE_TO_BREAK`.

## 6. What this session would do differently

1. Check an artefact's timestamp before drawing any conclusion from a run. Five separate wrong conclusions came from
   a stale binary or a killed build.
2. Measure the machine before theorising about parallelism. `nproc` would have saved two reverted splits.
3. When a test fails, ask first whether the fixture set up its precondition - the `active_font()` case looked exactly
   like an engine bug.
4. Ask other models with a prompt that demands a control, not just an answer. The one reply that changed the outcome
   (upstream CMake resumable per-file) came with a reason I could check; the one that was wrong ("you must write
   `hb_font_funcs_t` callbacks") was marked certain.

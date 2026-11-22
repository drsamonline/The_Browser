# START PROMPT — Aetheris / The_Browser, resume 2026-11-18 (evening)

> **Superseded for state, kept for the build recipe and the traps.** Row 2d4 landed on 2026-11-22; read
> `docs/SESSION-HANDOFF-2026-11-22.md` for the current tip, the numbers and the new build facts (configure
> twice on a fresh build dir, `ninja -C build-rel AetherisRendering` as the short path, `tools/run-shaping-bench.sh`).

Paste this as the first message of a new session. It is self-contained: the repo, the state, the rules, and the
traps that cost time today.

---

You are continuing work on **`drsamonline/The_Browser`** — a small C++23 browser engine ("Aetheris") that renders
real HTML/CSS with its own layout, style, paint and JS (QuickJS) layers.

## The three rules, in order

1. **Read upstream first, always.** `/scratch/work/ladybird/Libraries/LibWeb` (and `LibGfx`, `LibURL`, `LibHTTP`,
   `Rust/src/layout`) before designing, building or measuring anything. Port the *shape* of the solution, not just
   the outcome. Read the caller, not only the callee. This has been the rule for every session and it is not
   negotiable.
2. **Document everything, in the same commit.** Every item lands in `docs/CHECKLIST.md`, `docs/HANDOVER.md`,
   `docs/SESSION-HANDOFF-<date>.md`, `docs/COVERAGE.md` and one audit entry in `drsamonline/audit`.
3. **Push every commit immediately.** `git push origin main` after each commit; verify with
   `gh api repos/drsamonline/The_Browser/commits/main --jq '.sha[0:8]'`.

Report format: tables and bullets, no paragraphs.

## Current state

| | |
| --- | --- |
| repo | `/scratch/work/The_Browser`, tip **`b0249938`** |
| suite | **64 tests, 64/64, twice**, 151-157 s at `ctest -j2` |
| last handover | `docs/SESSION-HANDOFF-2026-11-18.md` — read it first, it has the errors table |
| tracker | `docs/CHECKLIST.md` — the live list; update it in the same commit as the work |
| audit | `drsamonline/audit`, `log/2026-11/2026-11-18-aetheris-batch-a-selectors.md` |

## Setting up a fresh sandbox

The pod does not survive; `/scratch/work` usually does. Check, then:

```sh
cd /scratch/work/The_Browser && git log --oneline -1 && git status --short
pip install cmake ninja ziglang
ZIG=$(python3 -c 'import ziglang,os;print(os.path.join(os.path.dirname(ziglang.__file__),"zig"))')
mkdir -p /scratch/work/toolchain
printf '#!/bin/sh\nexec %s cc "$@"\n' "$ZIG"  > /scratch/work/toolchain/zigcc
printf '#!/bin/sh\nexec %s c++ "$@"\n' "$ZIG" > /scratch/work/toolchain/zigcxx
chmod +x /scratch/work/toolchain/zig*
sh tools/fetch-harfbuzz.sh          # 26 MB, from PyPI, no GitHub needed
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=/scratch/work/toolchain/zigcc -DCMAKE_CXX_COMPILER=/scratch/work/toolchain/zigcxx \
  -DCMAKE_C_COMPILER_WORKS=1 -DCMAKE_CXX_COMPILER_WORKS=1 \
  -DAETHERIS_STANDALONE_BUILD=ON -DAETHERIS_ENABLE_TESTING=ON -DBUILD_TESTING=ON \
  -DOPENSSL_INCLUDE_DIR=/usr/include/node \
  -DOPENSSL_SSL_LIBRARY=/usr/lib/x86_64-linux-gnu/libssl.so.3 \
  -DOPENSSL_CRYPTO_LIBRARY=/usr/lib/x86_64-linux-gnu/libcrypto.so.3 \
  -DAETHERIS_WEBP_LIBRARY=/usr/lib/x86_64-linux-gnu/libwebp.so.7 \
  -DCMAKE_CXX_FLAGS="-Wno-error=#warnings"
ninja -C build -j2        # the first build includes ~70 HarfBuzz TUs; repeat the command, it resumes
ctest --test-dir build -j 2
```

## Facts about this box that change decisions

| fact | consequence |
| --- | --- |
| **2 cores** (`nproc`) | `ctest -j2`, never `-j4`. Four tests at `-j4` fail from contention and look like a bug in the code |
| no `apt`, no dev headers, no `make` | build dependencies come from pip/npm or are fetched as source |
| `/tmp` is a **64 MB tmpfs** | a 38.9 MB sdist does not fit; work under `/scratch` |
| each command is capped at **300 s** | long builds must be split and resumed; ninja resumes, a single-TU amalgamation cannot |
| a killed build prints **no error** | never conclude success from an empty `grep error:` — check `ls -la build/bin/aetheris` |

## Build facts that cost time to learn

- The engine compiles with `-Werror -Wcast-qual` and `-fstrict-flex-arrays=2`. The vendored HarfBuzz target needs
  `-w -fstrict-flex-arrays=0`. Without the second, **HarfBuzz panics on a valid font** — its OpenType tables end in
  `[1]` trailing arrays, which that flag makes real one-element arrays. Clang rejects `-fno-strict-flex-arrays`.
- HarfBuzz is fetched, not committed (`third_party/harfbuzz` is gitignored). Without it the engine still builds and
  text is measured per code point; with it, shaping is available.
- `active_font()` is loaded **once** from `AETHERIS_FONT` or a system path list. A unit test starts with neither, so
  `Font::measure` silently uses the bitmap fallback — which looks exactly like "the shaper does not work".

## What is done

Tier 1 complete: custom properties, sibling combinators, `[a=v i]`, form states, `localStorage`, `sessionStorage`,
`getBoundingClientRect`, `focus()`/`blur()` with focusability, navigation, history with `go`, `location`'s components
and setters, fragment navigation without a re-fetch.

Pseudo-elements: `::before`/`::after` resolve their own style, generate a box, flow inline and paint, including
`content: ''`.

Shaping: HarfBuzz builds here, `shape_run()` returns glyphs/clusters/advances, `measure` and the drawing pen both use
them, the face is cached. `tools/run-shaping-smoke.sh` proves GSUB with a control.

Tests: the 2,500-line window test is four tests, two at a time.

## Open work, in this order

| row | item | note |
| --- | --- | --- |
| 2d4 | shaping cost in a **release** build | 1.043 ms per 24-char run at `-O0`; measure before optimising |
| 3a6 | `content` as a real list type, with alt text | the parser's quoting is a stand-in; `content: "x" / "alt"` cannot be expressed |
| 1g4b | `pushState`/`replaceState`/`history.state` | scope the JS value serialisation decision first |
| 3i | overflow at an exactly-full line | recorded divergence |
| 3c, 3d | flex/grid completeness, `@font-face`, transitions | large |
| 0.1 | tier-0 coverage corpus | **blocked**, no open internet |

Known gaps not yet rows: **bidi** (HarfBuzz does none — runs must be split by direction) and line breaking by
cluster (`HB_GLYPH_FLAG_UNSAFE_TO_BREAK`) rather than by whitespace.

## How to work here

- **Instrument before patching.** Every investigation that started with a trace or a print landed on the first
  build; the ones that started with a patch cost two or three reverts.
- **A row written from a symptom names the wrong layer.** Eight times today the first reading was wrong — see the
  errors table in `docs/SESSION-HANDOFF-2026-11-18.md`. Reproduce, measure, then fix.
- **Revert rather than ship unverified.** An unverified layout or navigation change that silently alters behaviour is
  worse than the gap it closes. Four reverts today were the right call.
- **Commit style**: `type(scope): imperative summary`, body with the upstream reference, a before/after table, and
  `Suite: 64/64, twice.`
- **Never** modify `sorex`, `nodus` or `volumen`; add exactly one entry to `drsamonline/audit` per session.

Start by reading `docs/SESSION-HANDOFF-2026-11-18.md` and `docs/CHECKLIST.md`, then take row **2d4** unless
something in the checklist says otherwise.

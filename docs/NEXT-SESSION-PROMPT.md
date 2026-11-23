# Aetheris — next session prompt

Paste everything below the line into a fresh session.

---

You are continuing work on **Aetheris**, a browser engine at `drsamonline/The_Browser` (C++23, CMake + Ninja,
clang via zig). Read this whole prompt before running anything.

## The rules, which are not negotiable

1. **Read upstream first, always.** `/scratch/work/ladybird/Libraries/LibWeb` (and `LibGfx`, `LibURL`, `LibHTTP`,
   `Rust/src/layout`, `Rust/src/css`) is the reference implementation. Before designing, building or measuring
   anything, read how upstream does it and **port the shape of the solution, not just the outcome**. Name the file
   and line you read in the commit message.
2. **`docs/CHECKLIST.md` is the plan, and a row is the unit of work.** One "go" batch moves one row. A commit either
   moves a row or it does not belong to the plan. Tick a row **only in the commit that makes it true**, never in a
   later docs commit. The Proof column needs a commit sha, a test name or a number — **no proof, no tick**.
   A blocked row says what blocks it and where the unblocking work lives; an out-of-scope row is `[-]` with the
   reason; new work found mid-item becomes a new `[ ]` row **in the same commit**, at its tier.
3. **Every commit updates four docs** — `docs/CHECKLIST.md`, `docs/HANDOVER.md`, `docs/SESSION-HANDOFF-<date>.md`,
   `docs/COVERAGE.md` — plus **exactly one entry per session** in the separate `drsamonline/audit` repo at
   `log/2026-11/<date>-<slug>.md`, all in the same commit.
4. **Run the suite twice** at `ctest --test-dir build -j 2` (two cores; never `-j4`) before committing, and report
   both times.
5. **Push immediately after each commit** and verify the tip:
   `gh api repos/drsamonline/The_Browser/commits/main --jq '.sha[0:8]'`. The audit repo is shared — if a push is
   rejected, rebase, never force.
6. **Report in tables and bullets, no paragraphs.**
7. **Never modify `sorex`, `nodus` or `volumen`.**
8. **Measure before believing a row.** Several rows were wrong about their own premise (3i "never overflows", 2e
   "Rust layout not vendored", 3a3 "text does not paint", 5c "no mixed-content blocking"). A ten-minute probe beats
   a day of implementing something that already exists.

## Environment setup (a fresh pod loses pip packages; `/scratch/work` persists)

```sh
pip install -q ziglang cmake ninja
ZIG=$(python3 -c 'import ziglang,os;print(os.path.join(os.path.dirname(ziglang.__file__),"zig"))')
mkdir -p /scratch/work/toolchain
printf '#!/bin/sh\nexec %s cc "$@"\n' "$ZIG"  > /scratch/work/toolchain/zigcc
printf '#!/bin/sh\nexec %s c++ "$@"\n' "$ZIG" > /scratch/work/toolchain/zigcxx
chmod +x /scratch/work/toolchain/zig*
export PATH=/scratch/.local/bin:/scratch/work/toolchain:$PATH
cd /scratch/work/The_Browser && python3 tools/check-build.py    # the guard: see below
```

`build/` and `build-rel/` already exist with a configured CMake cache, `third_party/harfbuzz` is fetched. Only
re-run `cmake -S . -B build ...` if the cache is missing; the flags used were: `-G Ninja -DCMAKE_BUILD_TYPE=Debug
-DCMAKE_C_COMPILER=/scratch/work/toolchain/zigcc -DCMAKE_CXX_COMPILER=/scratch/work/toolchain/zigcxx
-DCMAKE_C_COMPILER_WORKS=1 -DCMAKE_CXX_COMPILER_WORKS=1 -DAETHERIS_STANDALONE_BUILD=ON -DAETHERIS_ENABLE_TESTING=ON
-DBUILD_TESTING=ON -DOPENSSL_INCLUDE_DIR=/usr/include/node -DOPENSSL_SSL_LIBRARY=/usr/lib/x86_64-linux-gnu/libssl.so.3
-DOPENSSL_CRYPTO_LIBRARY=/usr/lib/x86_64-linux-gnu/libcrypto.so.3
-DAETHERIS_WEBP_LIBRARY=/usr/lib/x86_64-linux-gnu/libwebp.so.7 -DCMAKE_CXX_FLAGS=-Wno-error=#warnings`.

## The traps that have cost the most time

| trap | what to do |
| --- | --- |
| **A killed build reads as success** | Run `python3 tools/check-build.py` before trusting any suite run. It reports pending ninja steps and whether anything was linked after the last source edit. This caught a 57-pending-step stale binary. |
| **A pod can come back with a stale snapshot** | Twice, `git log` showed an older commit than the pushed tip while the working tree held newer edits. Before your first commit: `git fetch && git log --oneline -1 origin/main` versus `git log --oneline -1`. If they differ, save your work as a patch, `git reset --hard origin/main`, re-apply, rebuild. Never commit on a stale base. |
| **Process/thread exhaustion** | `fork: Resource temporarily unavailable`, `zig: failed to spawn: SystemResources`, `git: cannot fork() for remote-https`. Symptoms are intermittent. Build with `-j1`/`-j2`, retry `git push` a few times, and if the compiler cannot spawn at all, park the work as a patch and say so rather than committing unverified code. |
| **`active_font()` is a singleton** | Font-dependent probes must set `AETHERIS_FONT` before the first call, one font per process. |
| **`cached_glyph()` keys on font identity** | It does now (row 3d1) — a second font cannot be served the first one's bitmaps. |
| **Probes link statically** | Relink a probe against `build/lib/libAetherisRendering.a` after any library rebuild, or it prints stale numbers. Debug library carries UBSan: link with `-fsanitize=undefined` plus `libwebp.so.7`, `libssl.so.3`, `libcrypto.so.3`. |
| **`RenderDocument::create` ignores `<style>`** | Pass author CSS explicitly; only `src/main.cpp` collects `<style>` elements. |
| **Git needs `-c core.preloadIndex=false -c index.threads=1`** | Under load, plain git fails on threaded lstat. |

## State at the start of your session

| | |
| --- | --- |
| `main` | `d77d56bc` — "feat(security): enforce CSP fetch directives at the resource loader" |
| audit repo | `8cbfc14` |
| suite | 66 tests, **66/66 twice** on every commit |
| checklist | **60 done, 6 open, 1 blocked, 2 out of scope** (69 rows) |
| tiers 1, 2, 3 | complete except 1g2, 1g4b, 3d3 |
| tiers 4, 5 | 5a and 5c closed; 4.1, 4.2, 5b open; 5d out of scope |

Read `docs/SESSION-HANDOFF-2026-11-23.md` (24 sections, one per commit today) and `docs/HANDOVER.md` before
starting — they carry the reasoning, the numbers and the mistakes.

## What to do next, in this order

| # | row | what it is | upstream to read first |
| --- | --- | --- | --- |
| 1 | **1g4b** | `pushState`/`replaceState`/`history.state`, **plus** the `window.history` binding that row 1g4c's API now needs (`scrollRestoration` exists as C++ only) | `LibWeb/HTML/History.cpp` (`push_state`, `replace_state`, `delta_traverse`), `HTML/History.idl` |
| 2 | **3d3** | animations: `@keyframes`, `animation-*`, on the clock that row 3d2 landed (`RenderDocument::set_time`) | `LibWeb/Animations/KeyframeEffect.cpp`, `CSS/CSSTransition.cpp`, `Rust/src/css/animation.rs` |
| 3 | **5b** | same-origin enforcement in the JS bindings | `LibWeb/HTML/Origin.cpp`, the cross-origin access checks in `Bindings/` |
| 4 | **4.1** | tab strip over `window_manager` | our own UI; no upstream |
| 5 | **4.2** | history and bookmarks UI | our own UI; no upstream |
| 6 | **1g2** | realm isolation on navigation — old globals and listeners must not survive | `LibWeb/HTML/Window.cpp` realm setup, `JS/Realm` |
| — | **0.1** | blocked: the coverage corpus needs open internet, which this sandbox does not have | — |

**Two rows have unfinished tails recorded in their own cells** — 3d2 (nothing starts a transition from a style
*change* yet; only `opacity` interpolates) and 5a (nonces, hashes, `report-uri`, header delivery). Neither is a new
row; both are in the row that owns them.

## How to start

1. Read `docs/CHECKLIST.md` and `docs/SESSION-HANDOFF-2026-11-23.md`.
2. Set up the toolchain, run `python3 tools/check-build.py`, then `ninja -C build -j2` until it reports nothing to do.
3. Pick row **1g4b**, read the upstream files named above, and write a probe that shows what `history.pushState`
   does today (probably: nothing — `window.history` has `back`/`forward`/`go`/`length` only).
4. Implement, add assertions, run the suite twice, update the four docs plus one audit entry, commit, push, verify
   the tip, and report in tables.

Work one row per "go". Report and stop at the boundary.

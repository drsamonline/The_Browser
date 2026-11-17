# Aetheris — session handoff, 2026-11-17

Read this first, then `docs/HANDOVER.md`. This file covers one session; the handover covers the project.

## State of the tree

| Item | Value |
| --- | --- |
| `main` tip | `408c4c1a` — `perf(style): per-node invalidation with a style cache`, then `c884bf23` (docs) |
| Suite | 59/59 passing on that commit |
| Audit log | `drsamonline/audit` at `b8b9d53` |

### Uncommitted work — **resolved, shipped 2026-11-17**

Incremental layout (first increment) was **applied, built and green (59/59) but not committed** when the
sandbox began failing with HTTP 500, and was treated as lost. It was re-applied from the design below in
the resume session and committed. See "Resumed session" at the end of this file; the design below is kept
because it is still the specification the code follows.

## What shipped this session

| Commit | What | Evidence |
| --- | --- | --- |
| `408c4c1a` | per-node invalidation + style cache | 410 → 5 resolves, 405 hits |
| `261feebe` | five attribute selector operators | 10 rows, both directions |
| `2133e576` | `[a]`, `[a=v]` | pixel + relayout counts |
| `48089be1` | attribute-write gate | `data-*` 6 → 0 relayouts |
| `c50040c9` | pseudo-class gate | no `:hover` rule → 0 relayouts |
| `ae20834e` | `activeElement` → `body` fallback | `thebody` from a timer |
| `0cbced61` | `:focus-within` covered for Tab | ancestor red, child keeps its colour |
| `79466784` | "check the instrument" in HANDOVER + UPSTREAM-FIRST | — |
| `b71a0f8`, `efe46a7`, `2d2ef9c` | same rule in the three family `AGENTS.md` | — |

Earlier in the day, and still current: `3193327f` (Tab order + `need_render` fix), `bcb74730` (Tab
regression test), `fb956755` (`keyup`), `4388fef3` (`:focus-visible`), `8ee71471` (links/controls
focusable), `0fe673cf` (form controls visible), `dd9a0cc0` (field width), `e6c11b6b` (text-entry test).

## The uncommitted design — incremental layout, first increment

Goal: skip the layout pass when only paint-affecting properties changed.

1. `dom_mutation`: a text write records its node **and clears** the attribute name
   (`note_text_mutation`), so a caller can tell text (can reflow) from an attribute write (may only
   repaint). `note_dom_mutation()` (structural) clears both.
   **Order matters:** `note_dom_mutation_at(node)` clears the attribute name, so in `js_set_attribute`
   it must be called *before* `note_attribute_mutation(name)`. Getting this backwards broke the
   attribute gate and the suite caught it with `FAILED: an unobserved attribute write costs no relayout`.
2. `RenderDocument` gains `layout_key(StyleProperties)` — the ~40 layout-affecting properties joined —
   plus `m_layout_keys`, `m_geometry` (`rect`, `box`, `text_fragments` per DOM node), `m_layout_passes`.
   The property list: `display`, `width`, `height`, `min-width`, `max-width`, `min-height`, `max-height`,
   `margin*`, `padding*`, `border*` widths, `font-size`, `font-family`, `font-weight`, `line-height`,
   `white-space`, `position`, `top`/`right`/`bottom`/`left`, `float`, `overflow`, `overflow-wrap`,
   `text-align`, `letter-spacing`, `word-spacing`.
3. In `relayout()`: `text_or_structure = mutated == nullptr || last_attribute_mutation().empty()`.
   Compare every node's layout key against the previous pass. If neither changed and geometry exists,
   **skip** `LayoutEngine::layout` and copy geometry over node-for-node; else run the pass and re-snapshot.
4. Summary prints `layouts:` (cumulative) beside `relayouts:`, `resolves:`, `style hits:`.

Verification to run when re-applied:

| Page | Expect |
| --- | --- |
| `class` toggle changing only `background` | relayouts 6, **layouts 1** |
| `textContent` write | relayouts 6, layouts 6+ |
| idle | relayouts 0, layouts 1 |

Also assert a colour change still repaints (pixel), because a skipped layout that also skips the paint is
the stale-frame bug this project has shipped twice.

## Environment recipe

    pip install cmake ninja ziglang
    # wrappers: /scratch/work/toolchain/zigcc and zigcxx -> zig cc / zig c++
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_COMPILER=/scratch/work/toolchain/zigcc -DCMAKE_CXX_COMPILER=/scratch/work/toolchain/zigcxx \
      -DCMAKE_C_COMPILER_WORKS=1 -DCMAKE_CXX_COMPILER_WORKS=1 \
      -DAETHERIS_STANDALONE_BUILD=ON -DAETHERIS_ENABLE_TESTING=ON -DBUILD_TESTING=ON \
      -DOPENSSL_INCLUDE_DIR=/usr/include/node \
      -DOPENSSL_SSL_LIBRARY=/usr/lib/x86_64-linux-gnu/libssl.so.3 \
      -DOPENSSL_CRYPTO_LIBRARY=/usr/lib/x86_64-linux-gnu/libcrypto.so.3 \
      -DAETHERIS_WEBP_LIBRARY=/usr/lib/x86_64-linux-gnu/libwebp.so.7

A full build takes three `cmake --build` calls of ~280 s each. The sandbox recycles often: after a
recycle the local tree may be **stale** (it came back at `8b03b68d` twice) — `git pull --ff-only` first,
and check that a symbol you expect (`set_focus`, `layout_passes`) actually exists before editing.

## Working rules this session earned

1. **Read upstream first.** `docs/UPSTREAM-FIRST.md` has the feature→file map.
2. **Check the instrument before the subject.** Three investigations blamed the engine and were wrong
   three times: a pixel sample read as a picture, a verdict string computed backwards, an assertion whose
   colour test also matched white. Print raw values; test a colour as that colour
   (`r > 200 && g < 100 && b > 200` for magenta, never `r > 200 && b > 200`).
3. **Two probes, then park.** Every long investigation today was the instrument, not the code.
4. **Batch.** Several items per build, one audit entry per batch. Single small steps cost four calls each.
5. **Never ship a green test you cannot explain.** Tune nothing until it passes.

## Open questions, parked

| Item | State |
| --- | --- |
| `display` on an anchor | **closed, not reproducible** — four combinations identical; the side-by-side render came from a build that no longer exists |
| Harness vs CLI | **resolved** — the harness was right; my negative assertion matched white |
| Cache across interaction-state changes | needs upstream's `ancestor_traversal_for_pseudo_class` |
| Family P0 builds | blocked: no `make`, no `autoconf`, no dev headers in this sandbox |

## Next, in order

1. ~~Re-apply and **commit** incremental layout~~ — done in the resume session; see below.
2. Cache across interaction-state changes (upstream's `ancestor_traversal_for_pseudo_class`).
3. `[a=v i]` case flag, if a page needs it.
4. Family P0 builds on a machine with dev packages; record versions in each `SOURCES.md`.

---

## Resumed session — same day, from `c884bf23`

Re-applied the incremental-layout design from Appendix B, from scratch, and committed it. The design was
intact in the handoff; only the code had been lost.

| Commit | What | Evidence |
| --- | --- | --- |
| `1114541e` | text write vs attribute write (`note_text_mutation`, `note_dom_mutation_at` clears the name) | 59/59, no behaviour change on its own |
| `1b2417d7` | skip the layout pass when no layout key changed; `layouts:` in the summary | see the table below |
| `ac4f1fdf` | `AetherisWindowScripts`: a relayout is not a layout, four runs | 59/59 |
| `833ecabd` | remove the frame dumps on the way out | 59/59 twice in a row |

The three-row verification table, measured with 20 frames at 16 ms per frame, plus one control the design
did not have:

| Page | relayouts | layouts |
| --- | --- | --- |
| class toggle changing only `background` | 6 | **1** |
| `textContent` write | 6 | 7 |
| idle | 0 | 1 |
| class toggle changing `width` (control) | 6 | 3 |

The control is the point: the skip is conditional on the layout keys, not unconditional. A skip that
cannot fail proves nothing.

The pixel was read, not inferred: on a 2-frame run the div at (200, 84) goes `(0,0,255)` to `(255,0,255)`
with `layouts: 1`, and (500, 84) stays white. So a skipped layout still repaints — the stale-frame bug this
project has shipped twice is still not present.

### Two instrument failures, both environmental

1. **A full `/tmp` looked like a bad render.** The suite writes ~58 MB of frames into a 64 MB tmpfs and
   never removed them, so the second run in a sandbox started full: `AetherisDomHost` failed "render exits
   0" and `AetherisWindowScripts` failed "the repaint pixel is readable" on zero-byte frames. Both passed
   on a clean `/tmp`. Fixed by clearing the dumps at the end of the window test; recorded in `HANDOVER.md`.
2. **`zig cc` with `-Og` does not define `__OPTIMIZE__`**, so `_FORTIFY_SOURCE=3` warns and `-Werror`
   turns it into a build failure on the first translation unit. `-DCMAKE_CXX_FLAGS="-Wno-error=#warnings"`
   builds; fortify is inert at `-Og` either way. Worth knowing before blaming the tree.

### Second item: cache across interaction-state changes — done

`apply_visual_state()` no longer clears the cache and rebuilds every node. It invalidates the elements whose
pseudo-class state actually moved, following upstream's `ancestor_traversal_for_pseudo_class()`, and takes the
same geometry skip `relayout()` takes. The first pass now fills the cache too, so the first interaction has
something to reuse.

| Commit | What |
| --- | --- |
| `b2cc8a2f` | dirty set from the state move; cache and geometry kept; `:focus-within` subtree rule |
| `88e01bbd` | six runs in `AetherisWindowScripts`, three of them controls |

Measured, one interaction per run, 3 frames:

| page | before (relayouts/layouts) | after |
| --- | --- | --- |
| hover, background only | 1 / 2 | 1 / **1**, 5 resolves |
| hover, `width` (control) | 1 / 2 | 1 / 2 |
| hover, one row of 200 | 1 / 2 | 1 / **1**, 5 resolves, 401 hits |
| hover, `#card:hover .title` | 1 / 2 | 1 / **1** |
| `:focus-within`, background | 1 / 2 | 1 / **1** |
| `:focus-within`, `width` (control) | 1 / 2 | 1 / 2 |
| `:focus-within`, descendant | 1 / 2 | 1 / **1** |

The before column reported `resolves: 0` on every row because that path passed no stats to the builder — the
instrument had no reading for it, which is worth remembering before quoting a `resolves:` number from an old
run.

Two things the dirty set deliberately does **not** narrow:

- A **focus** move dirties the ancestor's whole subtree, because `#card:focus-within .title` matches a
  sibling of the focused input. Upstream narrows this with selector analysis; without one, a focus move
  re-resolves most of the page and gains only the layout skip. Hover and `:active` gain both.
- **Siblings** are never dirtied: `style.cpp` splits selectors on descendant and child combinators only, so
  `+` and `~` cannot reach a sibling. If they are ever matched, that is the place to fix it.

### Next, from here

1. Narrow the focus-move dirty set: the whole-subtree rule is correct but coarse. Selector analysis — does
   any rule mention `:focus-within` on this ancestor — is what upstream uses instead.
2. Extend `kLayoutProperties` when a property is added to `style.cpp` — a missing entry is a silent
   stale-geometry bug, not a slow path.
3. `[a=v i]` case flag, if a page needs it.

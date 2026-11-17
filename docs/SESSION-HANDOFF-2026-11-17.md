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

### Third item: narrow the focus-move dirty set — done

A focus move dirtied every ancestor's whole subtree. That is only needed when a selector can put
`:focus-within` on an **ancestor** of the subject — `.card:focus-within .title`. `CssStyleSheet` gains
`uses_pseudo_class_before_combinator()`, and the subtree walk is skipped when no rule of that shape exists.

| Commit | What |
| --- | --- |
| `6cf3becb` | the sheet-wide question, and the guard on the subtree walk |
| `3ba021cf` | two runs over the same 200-row page, one with such a rule |

| page | resolves | hits | layouts |
| --- | --- | --- | --- |
| `#card:focus-within` only | **5** | 401 | 1 |
| plus `#card:focus-within .title` | 408 | 0 | 1 |

The second row is the control: it shows the first is not passing because the sheet is ignored. One such rule
anywhere turns the coarse path back on for every focus move — the honest limit of a sheet-wide answer, where
upstream has the per-element one.

### Fourth item: per-element selector analysis — done

The sheet-wide answer from the third item let one `.card:focus-within .title` rule make every focus move in
the document coarse. `StyleResolver::matches_ancestor_pseudo_class_rule()` extracts the compound holding the
pseudo-class (`.card:focus-within` out of `.card:focus-within .title`) and matches it against the ancestor,
which is the question upstream's selector analysis asks. The sheet-wide helper is removed, not kept.

| Commit | What |
| --- | --- |
| `45b258b9` | the per-element question; the sheet-wide helper deleted |
| `de915893` | three runs, one of them the control |

| rule | focus | resolves | hits |
| --- | --- | --- | --- |
| `#card:focus-within .title` | outside the card | **4** | 405 |
| `#card:focus-within .title` | inside the card | 7 | 402 |
| `body:focus-within .title` | inside the card | 408 | 1 |

All three keep `layouts: 1`. The first row is the win — a rule on another card no longer costs the page —
and the third is the control: a compound that matches a wide ancestor still re-resolves everything.

Two fixture traps, both the instrument and both found by reading pixels:

- `#card .title` as the base rule outranks `body:focus-within .title` (10100 against 202), so the rule never
  applied and the page looked as if the matcher were broken.
- A bare `:focus-within .title` matches nothing: this parser needs a tag or class in the compound.

The test now pins the focus targets by window coordinate, calibrated against pixels first (`--focus-key`
takes a window y and the window subtracts the 34 px chrome).

### Fifth item: the layout key was an allow-list, and it was incomplete

Audited the layout key against the properties `layout.cpp` reads. Nine were missing — `gap`, `row-gap`,
`column-gap`, `flex-direction`, `flex-grow`, `align-items`, `justify-content`, `grid-template-columns`,
`aspect-ratio` — and a class toggle changing only `gap` skipped the pass and left the second box at x 50.
A stale frame, of exactly the kind the mechanism exists to prevent.

| Commit | What |
| --- | --- |
| `89d03716` | the key is the whole style minus a deny-list of paint-only properties |
| `a00d1bf2` | a `gap` change must reflow; a `color` change must not |

| page | before | after |
| --- | --- | --- |
| only `gap` changes | layouts 1, box at x 50 | layouts 2, box at x 250 |
| only `background` changes | layouts 1 | layouts 1 |
| only `color` changes | layouts 1 | layouts 1 |

`StyleProperties::digest_excluding()` carries it, and `fingerprint()` is now `digest_excluding({})`, so the
cache key and the layout key share one implementation and one sort.

An allow-list fails unsafely: a property nobody classified silently stops reflowing. A deny-list of
paint-only properties fails the other way — a wasted pass — which is the failure this project can afford.

One fixture trap: my first fixture used `flex-grow` and proved nothing, because its children had no `height`
and every probe fell below them. `flex-grow` itself is fine — see the sixth item, which took that claim back.

### Sixth item: `flex-grow` — no bug, and a claim taken back

The fifth item left `flex-grow` on the list as "read by `layout.cpp` and appears to have no effect". That was
wrong, and it was wrong in a pushed commit message as well, which cannot be rewritten — so it is corrected
here and in the audit entry.

`layout_flex_children()` implements it (`width = free_space * grow / total_grow` for an item with no
`width`), and `TestAetherisFlexLayout` has covered it since before this session ("flex-grow expands both
items").

My fixture was wrong: the children had no `height`, so they were one text line tall and every probe at
y = 84 read the *parent's* background. Re-measured with `height:100px` on both children and
`flex-grow: 1` against `flex-grow: 2` on a 400 px row:

| fixture | x=100 | x=150 | x=200 | x=300 |
| --- | --- | --- | --- | --- |
| `flex-grow: 1` / `2` | magenta | green | green | green |
| no `flex-grow` | blue | blue | blue | blue |

133 px of magenta is the 1:2 split of 400 px. The engine is right; the measurement was not. Third time this
session, and the same cause every time: a coordinate that did not mean what I assumed.

No code change. A window-level test for `flex-grow` would duplicate `TestAetherisFlexLayout`, which asserts
the widths directly rather than through pixels.

### Seventh item: two CSS features closed by evidence, and the key walk narrowed

**`[a=v i]` and sibling combinators — not built.** The brief for both was "if a page needs it". No page in
this tree needs either: no fixture, test page or stylesheet under `Tests/`, `docs/` or `src/` uses the `i`
flag, and none uses `+` or `~` between compounds. The only `[x=y i]` hits are in the vendored LibWeb tree,
which Aetheris does not render. Recorded rather than built.

The sibling case is worth restating because the invalidation code leans on it: `style.cpp` splits selectors on
whitespace and `>` only, so a `+`/`~` selector matches *nothing* — a silent no-op, not a wrong answer. If
anyone adds them, `collect_state_change_dirty()` has a comment at the exact place that has to change with it.

**The key walk, narrowed.** The remaining measured cost of the layout skip was that it digested every node's
style on every relayout. Only a node whose style was *re-resolved* can have a different key, so
`LayoutTreeBuilder` now reports the re-resolved set and the comparison walks that.

| Commit | What |
| --- | --- |
| `9aeaf9b0` | `restyled` out-parameter; filtered comparison; `key checks:` in the summary |
| `4c1fdffb` | the test asserts `key checks == resolves` |

| measurement, 200-row page (406 nodes), one hover | value |
| --- | --- |
| key comparisons, before | 406 |
| key comparisons, after | **5** |
| layouts | 1 |

`key checks:` is in the window summary beside `layouts:` and `resolves:`, so the cost of the decision is a
number rather than an inference.

### Eighth item: per-node layout — the pass stops entering clean subtrees

The skip was all-or-nothing for the document. Now a node whose subtree nothing restyled, and that is laid
out at the origin and available width it was laid out at last time, takes its recorded geometry and the pass
never enters it.

| Commit | What |
| --- | --- |
| `f0ce1b21` | `LayoutReuse` context; `reuse_subtree()`; origin recorded per node; `layout nodes:` in the summary |
| `5606dc69` | the text-edit case and the moving-row control |

| 405-node page, one edit, 2 frames | layout nodes |
| --- | --- |
| text edit in row 100 of 200 | **4** |
| first row grows 3 px → 60 px (control) | 204 |

The control matters: every row below the grown one moved, so none of them can be reused where it used to be,
and the pass lays them out. Pixels read off the frame: content y 1 blue (inside the grown row), content y 70
green (the first row after it).

### The invariant, stated because the change rests on it

A node's subtree geometry is a function of (its resolved style, x, y, available_width, its DOM subtree).

- The style is fixed for a clean node: any style change marks the node restyled, and any structural change
  clears the style cache so every node restyles.
- x, y and available_width are exactly what the parent decided, which is why all three are in the cache key
  and why a node that moves — or whose width changes because a sibling grew — is laid out again.
- Anything that makes geometry depend on something outside that tuple breaks the invariant. `float` is not
  implemented, and flex items get their width from the parent, so nothing in the current engine does.

### Next, from here

1. `[a=v i]` and `+`/`~` remain unbuilt by evidence; revisit only when a page needs them.
2. Family P0 builds on a machine with dev packages; record versions in each `SOURCES.md`.
3. The window summary now carries four counters (`relayouts`, `layouts`, `key checks`, `layout nodes`) —
   enough that a fifth should replace one rather than be added.

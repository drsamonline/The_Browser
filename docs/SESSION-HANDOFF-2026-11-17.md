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
| text edit in row 100 of 200 | **5** |
| first row grows 3 px → 60 px (control) | 405 |

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

### Ninth item: the instrument, and a reference to check the reuse against

Two things, one of them the more useful.

**`layout nodes:` was under-reporting.** A text run is laid out by `layout_inline_children()` directly, not
through `layout_node`, so it was never counted — on a text-heavy page the number was about half the truth.
Text runs are counted now, and the first pass is counted too, which is what makes a mutated run and a fresh
one comparable at all.

| case | before | after |
| --- | --- | --- |
| text edit in row 100 of 200 | 4 | 5 |
| first row grows 3 px → 60 px | 204 | 405 |

**A differential test for the reuse path.** Every other test in the suite asserts what I expect. This one
compares two runs: a page mutated into a state, and the same page loaded in that state. Below the chrome they
must be byte-identical.

| run | layout nodes |
| --- | --- |
| mutated at runtime | 5 |
| loaded in that state | 405 |

Same pixels, 80× less layout work. The chrome is excluded because it carries the URL and the runs are served
on different ports — which is also how the first version of the comparison failed, and why the exclusion is in
the helper's name rather than in a comment.

### Fifth fixture error, and what caught it

The differential's first fixture dropped `.r` when it set `class='r hit'` — it set `class='hit'`. The row lost
its `height: 3px`, grew to a line, and pushed 200 rows down. The frame comparison was the wrong instrument to
catch that (both runs happened to agree on the shape); `layout nodes: 203` was the right one, because it said
the reuse had not fired. One temporary `fprintf` in `reuse_subtree` printed the origin mismatch —
`cached y=3 now y=19.2` — and the 16.2 px was a line height, which pointed straight at the missing rule.

That is the pattern for all five: a number that did not match what the code plainly does, chased with a
print rather than a theory.

### Tenth item: a click is not a layout

Not on the list. It came out of reading `dispatch_click_at()` while looking for the next item, and it was
the largest single waste the session found: the function called `relayout()` unconditionally, and
`relayout()` with no mutation on record treats the call as **structural** — it clears the style cache and
lays the page out from scratch. Clicking anywhere on a page whose handler only logged cost 407 resolves and
405 nodes laid out.

| click handler | before | after |
| --- | --- | --- |
| logs only | layouts 2, 405 nodes | layouts 1 (no pass) |
| writes an unobserved attribute | layouts 2, 405 nodes | layouts 1, 0 nodes |
| changes a width (control) | layouts 2, 405 nodes | layouts 2, 5 nodes |

It now relayouts only when the click mutated the DOM, gated the same way the timer and key paths already
were. The control still reflows.

**And an instrument fix it exposed:** both skip paths left `layout nodes:` reporting the *previous* pass, so a
relayout that skipped read as if it had laid the page out. That is how `click_attr` reported 405 before the
fix and 0 after — the second number is the true one. A counter that can be stale is worse than no counter,
because it looks like evidence.

### Eleventh item: upstream first, then a structural change that costs its sequence

Read upstream **before** writing this one, which was the point of the instruction.

**The inline item is closed by upstream, not built.** `DOM/CharacterData.cpp` marks the text node's own box
(`FfiBoxMarks::text_data_changed`) and lets the layout engine re-run the formatting context; the only
`set_needs_layout_tree_update` calls are for whitespace and emptiness, where a box may appear or disappear. So
upstream has no per-run text reuse at this layer to port — the formatting context is the unit, which is what
this engine already does. Building per-run reuse would have been an invention resting on this engine's
per-run wrapping, which a real line breaker would break.

**The structural item came out of the same reading.** Any structural mutation cleared the whole style cache.
Upstream records a tree delta for the sequence a node left or joined (`record_subtree_disconnecting`,
`PreviousSiblings`) and invalidates that. Ported:

| Commit | What |
| --- | --- |
| `06dd8bf4` | `note_child_list_mutation_at()`; the parent's subtree and ancestors instead of the cache; the parent alone when the sheet has no positional selector |
| `d1180e76` | three counts and the `:empty` pixel |

| 200-row page, one mutation, 2 frames | before | after |
| --- | --- | --- |
| append a span inside row 150 | 407 resolves | **6** |
| innerHTML replace inside row 150 | 407 resolves | **6** |
| append a div to body, no positional selector | 406 resolves | **5** |
| append a div to body, `:first-child` in the sheet | 406 resolves | 406 (on purpose) |

The fourth row is the control: a positional selector means any row's style can change when the sequence does.
The narrowing to "the parent alone" is sound because a positional selector is the only way one child's
presence restyles another, and the user-agent sheet has none — checked, not assumed.

### Twelfth item: reftests, in upstream's shape — and a SIGSEGV they found

Upstream verifies rendering with `Tests/LibWeb/Ref`: an input declares its reference with
`<link rel="match" href="...-ref.html">` and the two must render identically. That is the same idea as the
single differential added two items ago, so the item was to generalise it — upstream first, then the matrix.

| Commit | What |
| --- | --- |
| `1e7fb156` | five shapes, each mutated-vs-baked, byte-identical below the chrome |
| `253ab301` | the fix for what it found |

| shape | mutation |
| --- | --- |
| a text write | `textContent`, same length |
| a width change by class | class `r hit`, 400 → 200 px |
| a paint-only class change | class `r hot`, background only |
| an appended child | `appendChild` a styled span |
| an innerHTML replace | `innerHTML` = one styled span |

**What it found.** The innerHTML case crashed the browser:

    thread 29145 panic: member access within misaligned address ... for type
    'const aetheris::rendering::DomNode', which requires 8 byte alignment
    render_document.cpp:254 in relayout

`js_inner_html_set()` moved parsed children in with `push_back()`, so they kept the parent pointer they had
while being parsed — to the fragment root, which dies at the end of the block. Nothing walked `parent` after
an `innerHTML` set until the layout dirty-set walk did. `append_child()` sets the link, and every other
insertion path already used it.

That is the whole argument for a reference-based test: five cases agreed with my expectations and the sixth
was a use-after-free. Asserting what I expect would never have reached it.

**Two fixture errors, both mine.** `className` is not implemented here — only `classList` — so the appended
span had no class and the case failed for the wrong reason; `setAttribute` fixed it. And the failure message
had to name the case before it was worth reading, which is why `require` now gets a built string.

### Thirteenth item: classList was writing to the DOM and telling nobody

Upstream first: `DOM/Element.idl` has `[CEReactions, Reflect=class] attribute DOMString className` — a
reflected attribute, the `class` content attribute read and written, nothing more.

Looking for where to add it turned up something worse. `write_classes()` — the single place `classList`
writes — set `node->attributes["class"]` directly and noted **no mutation at all**. So `classList.add('on')`
on a page with `#a.on{background:...}` wrote the attribute, invalidated nothing, and left the frame blue.

| page | before | after |
| --- | --- | --- |
| `classList.add('on')` | relayouts 0, pixel blue | relayouts 1, layouts 1, pixel magenta |
| `className = 'on'` | relayouts 0, pixel blue | relayouts 1, layouts 1, pixel magenta |

Both now go through `set_class_attribute()`, which writes the attribute and notes it in the order
`js_set_attribute` uses — the node-scoped note first, because it clears the recorded attribute name.
`className` is a reflected accessor now, absent reading as `""`.

Both are in the reftest matrix, mutated against baked. That is the second bug in two items that the reference
comparison caught and an expectation would not have: I had no reason to suspect either.

**A third specificity trap, on the way.** My first `className` fixture used `#a{background:#0000ff}` against
`.on{background:#ff00ff}` — 10000 against 100 — so the rule never applied and the fix looked broken. Same trap
as the `body:focus-within .title` fixture earlier. The lesson has been written down twice and I walked into it
again; the tell is always a relayout count that is right and a pixel that is not.

### Fourteenth item: the audit, and one choke point instead of six

Upstream gets this from the type system: `Element::m_attributes` is private, and every write runs
`Element::set_attribute` → `run_attribute_change_steps` → `invalidate_style_after_attribute_change`. A caller
*cannot* write an attribute and forget to invalidate.

Here the map is public, so the audit was a grep for `attributes[...] =`. Six runtime writers noted nothing:

| writer | what a page does with it |
| --- | --- |
| `js_style_set_property` | `el.style.setProperty(p, v)` |
| `js_style_set` | `el.style = '...'` |
| `js_dataset_set` | `el.dataset.x = v` |
| `js_checked_set` | `el.checked = true` |
| `set_control_value` | `el.value = '...'` |
| `set_class_attribute` | `classList`, `className` (previous item) |

| write | before | after |
| --- | --- | --- |
| `el.style = 'background:#ff00ff'` | relayouts 0, blue | relayouts 1, magenta |
| `el.style.setProperty('background', …)` | relayouts 0, blue | relayouts 1, magenta |
| `el.dataset.x = '1'` | relayouts 0, blue | relayouts 1, magenta |

`set_attribute()` / `remove_attribute()` in `dom_mutation.hpp` are the choke point now. `js_set_attribute` and
`set_class_attribute` use them too, so the note order lives in one place instead of four.

**A fourth specificity trap.** The `dataset` fixture first used `#a{background}` against
`[data-x]{background}` — 10000 against 100 — and read blue. The relayout count said the note worked; the pixel
said the rule lost. That tell is now reliable enough to name: relayout right, pixel wrong, look at specificity.

### Fifteenth item: the CSSStyleDeclaration surface, and one thing not built

Upstream first, and it settled the question before any code was written: `CSS/CSSStyleDeclaration.idl` declares
`cssText`, `length`, `item(index)`, `getPropertyValue`, `getPropertyPriority`, `setProperty`, `removeProperty`
— and **no named-property accessor at all**. `el.style.background = v` is not part of the interface upstream
either. The way a page sets a property is `setProperty` or `cssText`.

So the item changed shape: not "add named properties", but "complete the declared surface".

| member | behaviour |
| --- | --- |
| `cssText` | get: the style attribute; set: writes it |
| `getPropertyValue(p)` | the declared value, or `""` |
| `removeProperty(p)` | returns the value it removed (was `undefined`) |
| `length` | number of declarations |
| `item(i)` | the property *name* at that position |

The declaration list is parsed once into a vector that both the accessors and `apply_style_property` share,
so `cssText` and `item()` agree with what `setProperty` wrote. Every write still goes through the attribute
choke point, so setting a property restyles.

Read back by the page in a test, not inferred: `cssText` = `background: #ff00ff`, `getPropertyValue` =
`#ff00ff`, `length` 1, `item(0)` = `background`, `removeProperty` = `#ff00ff`, `length` 0 after.

**The value of reading first, this time:** I was about to build a proxy so that `el.style.prop = v` would
work. Upstream's IDL says that is not the interface. Building it would have been a day of machinery for an API
that does not exist in the spec either — and the note in the handoff would then have been wrong in the other
direction, claiming a feature the engine does not need.

### Sixteenth item: `:empty` was not implemented, and a test had been lying about it

Looking for the next item, I probed `el.remove()` and `insertBefore` at runtime. Both work. Then the pixel on
the removal page was the wrong colour, and the reason was not the removal: **`:empty` was never implemented**.

Upstream's definition, from `DOM/SelectorQuery.cpp`: "Child nodes and their character data" — comments and
doctypes do not count, and neither does text of length zero. Answered before `element_position()` here,
because it is about the node's children rather than its position among siblings.

**The consequence is the part worth recording.** Two items ago I narrowed structural invalidation to the
parent, with this reasoning: "only the parent can change (`:empty`)". The reasoning was sound and the selector
it was for did not exist. The test I wrote for it asserted magenta after appending a child into an empty box —
and the base colour was magenta too, so it passed for two commits while testing nothing. A control that cannot
come out the other way is not a control; that rule is in `UPSTREAM-FIRST.md` and I broke it by writing an
assertion whose expected value matched the failure case.

| case | expect |
| --- | --- |
| an empty box matches `:empty` | blue |
| appending a child stops `:empty` matching | magenta |
| removing the only child matches again | blue |

The first row could not have passed before these two commits, which is what makes it a control.

### Seventeenth item: the structural pseudo-class set, and the list that must not drift

Upstream's set (`CSS/SelectorMatching.cpp`) is `:empty`, `:first-child`, `:first-of-type`, `:last-child`,
`:last-of-type`, `:nth-child`, `:nth-last-child`, `:nth-of-type`, `:nth-last-of-type`, `:only-child`,
`:only-of-type`, `:root`, plus `:not`, `:is`, `:where`, `:has` and the rest. This engine had four.

Added: `:root`, the -of-type family, `:nth-last-child(n)`, and `:not(<compound>)` — the argument goes back
through `matches_simple()`, so `:not(:first-child)` works.

| rule | matching row | others |
| --- | --- | --- |
| `div:first-of-type` | magenta | — |
| `div:nth-of-type(2)` | cyan | — |
| `div:last-of-type` | blue | — |
| `span:only-of-type` | yellow | — |
| `div:not(.a)` | rows 2–3 | row 1 green |
| `div:not(:first-child)` | rows 2–3 | row 1 green |

**Two things this needed beyond the rules themselves.** `element_position()` now counts by tag name as well
as by position, which is what -of-type means. And the compound scanner stopped at any marker, so `:not(.a)`
captured `"not("` and matched nothing — a functional pseudo-class now consumes to its matching `)`.

**The coupling is the point.** `uses_positional_pseudo_class()` is one list now, in `css_stylesheet.hpp` next
to the sheet, replacing the four-term check in `render_document.cpp`. Adding a pseudo-class to `style.cpp` and
forgetting the invalidation list was a two-file mistake waiting to happen, and the failure mode is a stale
frame — the worst kind here. A page using `:first-of-type` would have gone stale silently.

Guard: a 200-row page with `div.r:first-of-type` in its sheet re-resolves 405 nodes on an append, not 5.

### Eighteenth item: An+B, which is what `:nth-child(2n)` is for

Upstream's predicate is four lines (`Rust/src/css/style/selector.rs`):

    if step == 0 { return index == offset }
    let difference = index - offset;
    difference % step == 0 && difference / step >= 0

`difference / step >= 0` is what makes a negative step work: `-n+3` matches the first three and nothing after.
Ported with the microsyntax the arguments need — `odd`, `even`, an integer, `n`, `-n`, `2n`, `2n+1`, `-n+3`,
`+3n-2` — and anything else returns no match rather than a wrong one, which is what every form did before.

| rule | rows it colours (six-row list) |
| --- | --- |
| `nth-child(2n)` | 2, 4, 6 |
| `nth-child(odd)` | 1, 3, 5 |
| `nth-child(-n+2)` | 1, 2 |
| `nth-child(3n+1)` | 1, 4 |

Read off the pixels in cascade order. The zebra-stripe case is in the window test: `2n` and `3n+1` over six
rows, six pixels, and no row would have matched either rule before this.

### Where the session stands

47 commits between `966ab44c` and here, in four groups: incremental layout (skip, then per-node reuse, then the
key comparison), style invalidation (across state changes, per element, for structural changes), the DOM layer's
invalidation contract (one attribute choke point, six writers fixed), and the verification that found most of it
(the reftest matrix, which found a use-after-free).

### Next, from here

1. `:is`, `:where`, `:has` — each needs a selector-list argument; `:has` needs invalidation in the other
   direction (a child's change restyling an ancestor).
2. `[a=v i]` and `+`/`~` remain unbuilt by evidence; revisit only when a page needs them.
3. Family P0 builds on a machine with dev packages; record versions in each `SOURCES.md`.
4. `register_subtree()`/`purge_subtree()` no longer note a mutation — they only manage handles, and their
   callers note the child-list change. A new caller that forgets to will leave the page stale.

### Nineteenth item: `:is()`, `:where()`, list `:not()` — and two splitters that were not paren-aware

The sandbox recycled between sessions; the tree was re-cloned at `235f8956` and rebuilt from the recipe in
`START-PROMPT.md`, baseline 59/59 confirmed before any edit.

Upstream (`Rust/src/css/style/component_matching.rs`) matches all three over a **selector list**, and
`selector_arena.rs` scores them: `Is | Not => greatest(arguments)`, `Where => zero`, else one class.

| rule | rows coloured, six-row list |
| --- | --- |
| `:is(#r0, #r2, #r4)` | 1, 3, 5 |
| `:where(.r)` | none — zero specificity loses to `div.r` and `#r1` |
| `:is(.r, #r1)` | all six — 1 id, ties with `#r1`, wins on order |
| `div:not(#r0, #r1)` | 3, 4, 5, 6 |
| `#box :is(#r2, #r3)` | 3, 4 |

**Two splitters had to learn about parentheses, and the first one is the more dangerous kind of bug.**

- The rule parser split selectors on *every* comma token. `:is(#r0, #r2, #r4)` became `:is(#r0`, ` #r2` and
  ` #r4)` — and ` #r2` is a valid bare id selector, so it *matched*. A wrong rule that quietly works on one
  row is worse than one that fails: the pixel showed only the middle row magenta, which looks like a partially
  working feature.
- The combinator splitter read the space inside `:is(...)` as a descendant combinator, so after the first fix
  the selector became a descendant chain and matched nothing at all.

Both were found by reading pixels before writing assertions, which is the only reason the first one did not
ship as "`:is` mostly works".

### Next, from here

1. `:has(...)` — a child's change restyling an ancestor. The dirty sets already include ancestors of any
   mutation, so `:has(> .x)` and `:has(.x)` may need no invalidation change; `:has(+ .x)` cannot match until
   sibling combinators exist. Read `invalidate_style_after_pseudo_class_state_change` first.
2. `[a=v i]` and `+`/`~` remain unbuilt by evidence; revisit only when a page needs them.
3. Family P0 builds on a machine with dev packages; record versions in each `SOURCES.md`.
4. `register_subtree()`/`purge_subtree()` no longer note a mutation — they only manage handles, and their
   callers note the child-list change. A new caller that forgets to will leave the page stale.

### Twentieth item: `:has()`, and a capture that stopped at a marker

Upstream: `Pc::Has => self.matches_relative(arguments, node, node, context)` — a selector list of *relative*
selectors (optional combinator, descendant unless written), searched over what the element contains.

| rule | direct `.x` | `.x` two levels down |
| --- | --- | --- |
| `:has(.x)` | match | match |
| `:has(> .x)` | match | no |
| `:has(>.x)` | match | no |
| `:has(.nope)` | no | no |

**The invalidation needed no change**, which is the useful finding: `collect_dirty()` already includes the
ancestors of any mutation, so a child arriving restyles a `:has()` ancestor. Asserted by pixel rather than
assumed — "it already works" is a claim, not a reason to skip the test.

**The bug, and it took three probes.** `:has(> .x)` matched nothing while `:has(.x)` and `:has(> span)` both
worked. The compound scanner stops at `#`, `.`, `:` and `[`; my functional-pseudo fix-up fired only when the
captured value *ended* with `(`, which is true for `:not(.a)` and false for `:has(> .x)` — there the capture
stopped at the `.` after a space, so the pseudo text was `"has(> "`, with no `)` to find and the rule silently
dropped. It now consumes to the matching `)` whenever the capture stopped with an unclosed paren.

That is the third bug in this code path in two items — the rule parser's comma split, the combinator splitter's
whitespace, and now the compound scanner's marker. All three are the same mistake: **a parser for a nested
syntax that treats the inside of parentheses as flat.** Worth assuming there is a fourth.

### Next, from here

1. The fourth nested-syntax bug, if one exists: `specificity()` and `split_selector_list()` are the two places
   left that scan a selector character by character.
2. `[a=v i]` and `+`/`~` remain unbuilt by evidence; revisit only when a page needs them.
3. Family P0 builds on a machine with dev packages; record versions in each `SOURCES.md`.
4. `register_subtree()`/`purge_subtree()` no longer note a mutation — they only manage handles, and their
   callers note the child-list change. A new caller that forgets to will leave the page stale.

### Twenty-first item: custom properties and `var()` — the first item of the reviewed plan

The roadmap review put this first: parser-level, no layout work, the largest page-compat win per line. Upstream
(`Rust/src/css/custom_properties.rs`, `resolve_vars`) resolves against an inherited store with a substitution
context for cycles; this is the same shape with the ordinary property map, a prefix copy for inheritance and a
depth limit for cycles.

| case | result |
| --- | --- |
| `:root{--c}` → `var(--c)` | magenta |
| `var(--missing, #00ff00)` | green (fallback) |
| `--b: var(--a)` chain | magenta |
| `style="background:var(--c)"` | magenta |
| `var()` read two elements up | magenta |
| `--a: var(--b); --b: var(--a)` | exits 0, no substitution, no hang |

**Three orderings, each wrong on the first attempt** — and they are the whole item:

1. Custom properties inherit — `inherit_from()` copied a fixed list of named properties, so `--*` had to be
   added by prefix.
2. They are collected in their own pass — `:root{--c}` outranks `div{background:var(--c)}`, so a single pass
   substitutes the div before the variable is set.
3. They are inherited **before** the cascade — full inheritance runs at the end of `resolve()`, which is too
   late for substitution.

The first measurement said `(0,0,0)` on the `:root` case while the *fallback* case worked, which located the
problem in the lookup rather than the parser in one step: the fallback path never touches the store.

### Next, from here

1. The rest of the reviewed tier 1: sibling combinators with sibling invalidation, `[a=v i]`, the form-state
   pseudo-classes (`:checked`, `:disabled`, `:placeholder-shown`) — one parser+resolver cycle.
2. The coverage corpus (`docs/COVERAGE.md` has the commands) — blocked here, no open internet.
3. Font API: glyph ids and per-glyph advances before any rasteriser wiring.
4. Family P0 builds on a machine with dev packages.

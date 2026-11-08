# Aetheris build phases

**Author:** Dr. Sohil Momin · Part of the [Aetheris documentation set](USER_GUIDE.md)

The execution plan for the remaining work in [ROADMAP.md](ROADMAP.md). The order
is deliberate: **cheap, certain wins first, batched by shared code path; large
tasks last**, once the surface around them is stable.

## Rules

1. **Cheap and certain first.** A batch is sized so it can be built, tested and
   pushed in one cycle. Nothing large is started while small gaps remain.
2. **Batch by shared code path, not by feature name.** Items touching the same
   file or subsystem ship together, so one build and one test cycle covers them.
3. **Every batch ships independently** — each is pushed on its own, so value
   lands continuously and a stall in one batch blocks nothing else.
4. **No batch is done without end-to-end evidence.** Unit tests alone do not
   count; the behaviour must be exercised through the real pipeline.
5. **Large tasks come last** because they need the surrounding surface stable.
   Starting vector fonts before the text pipeline is settled would mean
   re-doing them.

## Phase A — Quick wins (5 small batches)

All small, low risk, each independently valuable. This is the phase to run first.

| Batch | Items | Files | Effort |
| --- | --- | --- | --- |
| **A1** | Event objects (`target`, `currentTarget`, `type`), `preventDefault`, `stopPropagation`, capture phase | `src/js/js.cpp`, `src/main.cpp` | S |
| **A2** | Networking polish: retry when a pooled keep-alive connection was closed; RFC cookie attributes (`Domain`, `Path`, `Expires`, `Secure`, `HttpOnly`) | `include/aetheris/network/http_client.hpp` | S |
| **A3** | DOM surface: `document.body`/`head`, `document.title`, `window.innerWidth`/`innerHeight`, `element.dataset`, `tagName`, `remove()`, `insertBefore` | `src/main.cpp` | S |
| **A4** | `getComputedStyle` (read-only resolved values) | `src/rendering/style.cpp`, `src/main.cpp` | S-M |
| **A5** | Developer speed: `aetheris --dump-dom <url>` printing the serialized DOM, so every later batch is easier to debug | `src/main.cpp` | S |

**Why first:** A1 unlocks forms and security work later; A3 and A4 are pure
surface additions with no layout risk; A5 pays for itself across every later
batch.

## Phase B — Forms (1 medium batch)

| Batch | Items | Effort |
| --- | --- | --- |
| **B1** | `input`/`select`/`textarea` `value` and `checked`, `submit` events, `preventDefault` (uses A1) | M |

**Why here:** forms are the largest user-visible gap that is still small, and
they depend on A1's `preventDefault`.

## Phase C — CSS breadth (2 medium batches)

| Batch | Items | Effort |
| --- | --- | --- |
| **C1** | Media queries, pseudo-elements (`::before`/`::after`) | M |
| **C2** | `transform` (translate/scale/rotate), `position: sticky` | M |

**Why here:** both are contained changes to the cascade/layout that do not
disturb the text pipeline, so they can land before the font work.

## Phase D — Real-time scheduling (1 medium batch)

| Batch | Items | Effort |
| --- | --- | --- |
| **D1** | `requestAnimationFrame`, `performance.now()`, optional real-clock timer mode | M |

**Why here:** builds directly on the virtual-time loop already shipped, and is
small once that exists.

## Phase E — Vector fonts (3 large batches)

The biggest visual win, and the biggest single task. Deliberately last among the
engine work.

| Batch | Items | Effort |
| --- | --- | --- |
| **E1** | TrueType/OpenType parsing: `cmap`, `head`, `hhea`, `hmtx`, `loca`, `glyf` → glyph outlines | L |
| **E2** | Rasteriser, integrated into the text layout path (replaces the bitmap font) | L |
| **E3** | Kerning (`kern`/`GPOS`) and a font fallback chain | M |

## Phase F — Network and security (3 batches, one large)

| Batch | Items | Effort |
| --- | --- | --- |
| **F1** | Certificate validation: hostname match, chain, expiry | M |
| **F2** | CSP, mixed-content blocking, same-origin enforcement in the JS bindings | M |
| **F3** | HTTP/2 | L — may stay deferred |

## Phase G — UI shell (large, multi-batch)

Window, tabs, address bar, bookmarks and history UI. The browser is headless
today; this is what makes it an application rather than a renderer.

## Deferred (not scheduled)

- Accessibility tree
- Developer tools / inspector
- Compositor-driven rendering
- Full-tree LibJS/LibWeb build (needs Rust, ICU, a clang plugin — a dependency
  decision, not a task)

## Verification recipe (every batch)

1. `cmake --build` the standalone profile.
2. `ctest --no-tests=error` — must stay green (51 tests today; new behaviour
   adds tests).
3. One end-to-end check through the real pipeline: a loopback page for DOM,
   networking and JS work; a rendered image for layout and paint work.
4. Update `CHANGELOG.md`, and `docs/ROADMAP.md` if a remaining item moved to
   done.
5. Push the batch on its own.

---

*Aetheris Browser — build phases. Authored and maintained by **Dr. Sohil Momin**.*

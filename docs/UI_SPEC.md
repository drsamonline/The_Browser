# Aetheris UI specification

**Author:** Dr Sohil Momin · Part of the [Aetheris documentation set](USER_GUIDE.md)

The browser chrome is deliberately minimal: the page should get as much of the
window as possible. This document fixes the proportions so the UI does not drift
larger as features are added.

## Principle

**No menubar.** There is no File/Edit/View row. Every control lives in one
horizontal strip, and that strip is as short as it can be while staying usable.
A conventional browser spends 80–120 px on chrome; Aetheris spends **34 px**.

## Layout

```
┌──────────────────────────────────────────────────────────────┐
│ ▭ title ×  +   ← → ↻   [ address… ]  ⋮                        │ 34 px
├──────────────────────────────────────────────────────────────┤
│                                                              │
│                     page content (734 px)                    │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

| Region | Height | Notes |
| --- | --- | --- |
| Chrome strip | **34 px** | Tabs, navigation, address bar and menu share one row |
| Page content | rest | 734 px of a 768 px window — **95.6%** |

For comparison, a conventional browser with a menubar and two toolbars leaves
roughly 85% of a 768 px window for content.

## Rules

1. **One strip only.** A second toolbar row is not allowed; new controls go into
   the existing strip or behind the menu.
2. **No permanent status bar.** Loading and error text is transient — a thin
   line or a brief overlay — never a reserved band.
3. **The address bar collapses to the URL when unfocused** and expands on
   `Ctrl+L`. It never occupies more than one row.
4. **Tabs are compact pills**, ~190 px wide, ~26 px tall, truncating with an
   ellipsis rather than growing.
5. **Zen mode** (`F11`) hides the strip entirely; `Ctrl+L` overlays the address
   bar so navigation stays reachable.

## Implementation

The chrome is **an HTML document rendered by Aetheris itself**, not a native
widget tree. It reuses the parser, cascade, layout, paint, anti-aliased text and
kerning that already exist, so the toolbar costs no new dependencies and looks
consistent with page content.

- `chrome_document()` in `src/main.cpp` builds the strip as a page.
- It is laid out with **absolute positioning**, not flexbox: the strip is a
  fixed 34 px row, so explicit coordinates are exact and predictable.
- `SoftwareSurface::blit` composites the chrome surface above the page surface.
- `aetheris --render-window <url> [out.ppm]` renders the composited result, so
  the chrome can be verified without a display.

## Window host

The window is behind a five-method interface (`src/ui/window_host.hpp`:
`open`, `present`, `poll`, `set_title`, `close`), so the chrome and input routing
never learn which platform they run on and a native backend can replace SDL2
later without touching them.

`aetheris --window <url> [frames] [dir]` runs the present-and-pump loop. With no
display backend it uses `HeadlessWindowHost`, which accepts a fixed number of
frames and writes each one to the given directory — that is how CI and the test
suite exercise the loop.

**Not yet written:** the SDL2 backend. This environment has no SDL2 headers and
no display, so a backend written here could not be compiled or verified. The
interface and the headless host are done and tested; the SDL2 implementation is
the next step and needs a machine that can build and run it.

## Known cosmetic gaps

- Navigation arrow glyphs render inconsistently in the strip; the tab, address
  bar and menu are correct.
- Tab close (`×`) and menu (`⋮`) are drawn but not yet interactive.

## Not in scope

Bookmarks, history UI, settings, downloads manager, devtools, extensions, sync,
find-in-page. Each is a feature a minimal browser does not need to be usable,
and each adds surface area that must be maintained.

---

*Aetheris Browser — UI specification. Authored and maintained by **Dr Sohil Momin**.*

# litehtml layout experiment

**Question.** Would adopting [litehtml](https://github.com/litehtml/litehtml) (BSD-3, C++, CSS2/CSS3
layout with a pluggable drawing container) improve this engine enough to justify
replacing the hand-written layout and paint stages?

**Short answer: not now.** It builds and it works, but the cost is architectural
rather than a matter of bytes, and the bytes are not small either.

## What was measured

Environment: the project's own toolchain (zig cc/c++, `-Os`, C++17), litehtml
built from source with its bundled gumbo.

| Item | Result |
| --- | --- |
| Build | clean, no patches needed |
| Linked probe, stripped | 1,561,776 bytes (~1.49 MB) |
| Of which gumbo (already shipped) | ~352 KB |
| **litehtml incremental cost** | **~1.1 MB** |

Projected binary if adopted: roughly **3.4-3.5 MB** (from 2.38 MB), assuming the
hand-written layout code it replaces is a few hundred KB.

## Layout quality

A minimal `document_container` (fixed-metric text, no drawing) laid out a test
page correctly and produced sane geometry:

```
<html> x=0 y=0 w=800 h=120
  <body> x=8 y=8 w=784 h=104
    <h1> x=8 y=8 w=784 h=16
    <p> x=8 y=24 w=784 h=16
      <> x=8  y=24 w=40 h=16
      <> x=48 y=24 w=8  h=16
      <> x=56 y=24 w=48 h=16
```

That is real CSS behaviour: the default 8px body margin, block flow, line boxes,
and inline runs advanced by measured text width. It is a genuine step up from
the hand-written layout.

## Integration cost

The `document_container` interface has **34 virtual methods** — text metrics,
fonts, images, four gradient kinds, borders, clipping, media features, language,
and element creation. A no-op container for a probe is ~50 lines; a real one
that draws into this project's `SoftwareSurface` and answers real font metrics
is a substantial piece of work on its own.

The larger problem is ownership. litehtml **parses** the HTML (with gumbo) and
owns its own element tree. This project's DOM is the thing the JavaScript
bindings mutate (`getElementById`, `innerHTML`, `textContent`, `classList`,
events, hit-testing), and those bindings are QuickJS objects wrapping our
`DomNode`. Adopting litehtml means either:

1. discarding our DOM and re-pointing every QuickJS binding at litehtml's
   element tree, or
2. keeping our DOM and using litehtml only as a layout engine, which is not how
   it is designed to be used.

Either way the work is a rewrite of the DOM/JS boundary that was just built and
tested, for a layout improvement.

## Decision

Not adopted. Recorded here so the option is not re-litigated from scratch.

If it is revisited, the trigger should be a specific layout feature this engine
cannot reasonably grow — multi-column, grid, or correct float interaction — and
the change should be planned as a DOM/JS re-binding, not as a drop-in library
swap.

Note that litehtml is honest about its scope: it is a lightweight renderer for
formatted content and simple pages, not a full modern engine, and it is not
fully HTML/CSS conformant.

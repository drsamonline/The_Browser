# Session handoff — 2026-11-25

## 1. First commit — the 22px, bounded and then left alone

Row 0.10's tail: the real page's `h1` sits at x=487 with a width of 534.84 in a container ending at
1000, so its right edge lands at 1021.84 - a 21.84px overshoot.

What this session added is a bound on the question rather than another attempt at the answer:

- The overshoot is **not** padding or border; those were fixed on 2026-11-24 and the fixture proves it
  (`padding: 0 10px` at `flex-end` moved from 260-319 to 240-299).
- The flex free space is computed from a base of **513**, and the placed box ends **534.84** wide. The
  base size and the placed box disagree by exactly the overshoot.
- `tools/article-position.cpp` now also prints the node's text-fragment count, and it says the `h1` has
  **0 fragments of its own** - its text is in a child. So `max_content_width()` (which measures text)
  and the child's actual layout are two different numbers for the same content.

The next question is which measurement is wrong, and the way to answer it is a fixture with one text
item in a flex row, comparing `rect.width` against `measure_text` of the same string - not another look
at the page.

### Why this thread was stopped

Three attempts, 22 pixels, one heading. The engine's real remaining gap is not this: it is that nobody
can say whether a render *matches* anything, because there is no reference to compare against. Every
defect found this week was found by a number contradicting an expectation, and the numbers are running
out of things to contradict. A reference render is the next instrument worth building.

## 2. Second commit — a reference render, because the numbers ran out of expectations

The week of 2026-11-23 found seven defects, every one because a number contradicted an expectation. By
the end of it the expectations were the scarce thing: the page rendered, and nothing could say whether
it rendered *right*.

### Chromium was already installed

`which chromium` finds one, so the reference is a real browser rather than a hand-written guess.
`tools/reference-render.sh`:

1. builds one **self-contained** document - the page's stylesheets inlined, its scripts dropped - so
   neither side needs the network and the comparison is about layout, not JavaScript;
2. renders it in chromium at 1024x768;
3. renders it in the engine at the same viewport;
4. reports sampled pixel agreement and the rows where link-coloured text lands in each.

### The first number

| | |
| --- | --- |
| pixel agreement, `en.wikipedia.org/wiki/Web_browser` | **44,390/49,152 = 90.3%** |
| contents-list item spacing, chromium | **27-28 px** |
| contents-list item spacing, engine | **79-80 px** |
| a minimal `<ul><li>` in both | **99.8%**, 19-20px on both sides |

So the largest visible difference is a spacing rule on the contents list that the engine applies at
roughly three times its size, and it is not list layout in general - the minimal list agrees almost
exactly. That 79px is what pushes the article title and body below the fold, which is the thing the
screenshots have been showing all week.

### What this changes about the work

Until now each defect was found by noticing something odd. From here the question is a number that goes
up or down: 90.3% today, and whatever it is after the next fix. That is a far better guide than reading
a render, and it costs about a minute per page.

### Next step, named in row 0.11

Extend `tools/article-position.cpp` to report the rules that set `margin`/`padding`/`line-height` on a
node named on the command line, and ask it about a contents item - the same way the `h1` at x=1000 was
explained by printing its ancestor chain.

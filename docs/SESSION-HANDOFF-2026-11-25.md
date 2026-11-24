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

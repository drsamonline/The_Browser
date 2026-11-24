# Session handoff — 2026-11-24

One commit: the corpus re-run that the layout fix made possible, and the row it produced.

## 1. First commit — the corpus re-run, and where a real page's time goes

Row 0.1 has been blocked on the sandbox's egress allowlist since 2026-11-23. The allowlist has moved
slightly since: **three** of the twenty corpus sites now answer (`example.com`, `en.wikipedia.org`, and
`www.debian.org`, which was 502 yesterday). Seventeen still return 502 or 403 from the proxy, so the
row stays blocked on the network, not on the work.

What changed is the one result that mattered. The first corpus run recorded wikipedia's 416,975-byte
page as *did not finish in 280 s*. It now renders:

| site | bytes | result |
| --- | --- | --- |
| `example.com` | 577 | rendered |
| `en.wikipedia.org/wiki/Web_browser` | 416,975 | **rendered, 57.8 s** |
| `www.debian.org` | 15,863 | rendered |

That is row 3k's fix showing up where it was found: the page was never slow to *parse* or to *style*,
it was laying out 2^depth nodes.

### The 57.8 s is not rendering

Two probes, both at `-O2`, on the same page:

| stage | cost |
| --- | --- |
| parse | 43.7 ms |
| CSS parse (UA + the page's 12,331 bytes of `<style>`) | 1.6 ms |
| style, 7,581 elements against 137 rules | 905.5 ms |
| layout | 67.9 ms |

**About one second of compute.** The rest is the page's **188 linked resources, fetched one at a
time**: the render took 57.8 s with the proxy set (every fetch succeeds) and 60.0 s with it unset
(every fetch fails), so the wall clock is the same whether the network works or not, at roughly 300 ms
per resource. The time is spent waiting, not working.

The second probe is new and is committed: `tools/css-cost.cpp` extracts a page's `<style>` blocks and
prices them, which `tools/layout-cost.cpp` does not do - it passes no author CSS, so the two were
measuring different work.

### What that produced

Row **0.4**: a page's linked resources are fetched serially. It is now the largest term in loading a
real page, and it is not a rendering problem. Upstream fetches subresources concurrently through its
event loop and a per-host pool; this engine has neither. The next step named in the row is a probe -
188 sequential fetches of one local resource against 188 concurrent ones - so the ceiling is a number
before anything is rewritten.

### The build state, honestly

No engine source changed in this commit: `docs/CHECKLIST.md`, `docs/COVERAGE-CORPUS.md` and the new
`tools/css-cost.cpp`. The pod was recycled between sessions, which invalidated the CMake cache, so
`tools/check-build.py` reports the full 300-step rebuild as pending. That is the guard doing its job,
not a broken tree: the commit is byte-identical to `fed64d9c` outside `docs/` and `tools/`, where the
last verified suite run stands at **67/67 twice**.

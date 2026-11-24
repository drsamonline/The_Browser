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

## 2. Second commit — row 0.4's ceiling, measured, and the fix deliberately not started

Row 0.4 asks for a probe before anything is rewritten: 188 sequential fetches against 188 concurrent
ones, so the ceiling is a number. That is `tools/fetch-cost.cpp`.

### The number

188 requests against a loopback server that models 20 ms of round trip:

| workers | time | speedup |
| --- | --- | --- |
| serial | 4,153 ms | 1.0x |
| 2 | 2,058 ms | 2.0x |
| 4 | 1,041 ms | 4.0x |
| 6 | 705 ms | 5.9x |
| 8 | 545 ms | **7.6x** |

Essentially linear, and the serial time sits on the arithmetic floor (188 x 20 ms = 3,760 ms). Applied
to the real page, whose fetches cost about 300 ms each, 57.8 s becomes roughly **8 s**.

### Two wrong probes before the right one

Worth recording, because both would have produced a confident wrong answer.

1. **The server answered one connection at a time.** The first version accepted, slept, replied and
   looped, so concurrency bought nothing and x2 measured 4,200 ms - a fact about the server, not the
   client. A real server answers concurrently; the probe's now uses a 16-thread pool.
2. **One thread per connection** then hit this box's thread limit ("Resource temporarily unavailable",
   the same trap the handover already lists) at 376 threads. A fixed pool of 16 fixed it.

The third version also had to detach the server thread: it accepts until the process ends, so joining
it hung the probe.

### The probe is raw sockets on purpose

The first version called `HttpClient::get()` from several threads, which races: the cookie jar, the
response cache and the connection pool are function-local statics shared by every thread. Making them
`thread_local` was written and measured, and then **reverted**, because verifying it needs a full
rebuild and the pod's recycled CMake cache put that at roughly forty minutes. Committing unverified
engine code to save a compile is not a trade this project makes, so the probe uses raw sockets and the
`thread_local` change ships with the fix.

### What the fix is, and why it is not a loop swap

`src/main.cpp` fetches every discovered dependency in a plain `for`. Making that concurrent needs two
things first: `SecureResourceLoader::load()` writes through to the shared `ResourceCache`, so only the
network part can be concurrent and the cache write has to stay on one thread; and `HttpClient`'s three
statics have to become per-thread. Both are named in the row, which stays `[ ]`.

## 3. The rebuild, finished

The pod's recycled CMake cache left 300 steps pending across the two commits above. They are done:

| check | result |
| --- | --- |
| `ninja -C build -j2` | complete, no errors |
| `tools/check-build.py` | all 68 binaries current, exit 0 |
| suite, run 1 | **67/67**, 123.47 s |
| suite, run 2 | **67/67**, 126.28 s |

So the tree at `73eaafbc` builds and is green, and the two commits that claimed no suite run are
covered by this one. **The next session does not need to rebuild**: `/scratch/work` survives a pod
recycle with both build directories intact, and only the pip toolchain has to be reinstalled (see
`docs/HANDOVER.md` 7f).

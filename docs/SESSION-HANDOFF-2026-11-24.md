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

## 4. Fourth commit — row 0.4: concurrent subresources, and a premise that was wrong twice

The fetch loop in `src/main.cpp` is now a bounded pool: one worker per sixteen dependencies, capped
at eight. Each worker builds its **own** `SecureFetcher` - cache, backends and security policy -
because a fetch writes into all three, so sharing one across threads is a data race rather than a
saving. Results come back into a vector and are applied on one thread in discovery order, so the
cascade keeps the order the document declared. `HttpClient`'s cookie jar, response cache and
connection pool are `thread_local` for the same reason.

### What it actually bought: about 10%, not 7.6x

The probe said 7.6x at eight workers. The page says ~10%. Both are right, and the difference is where
they ran:

| | |
| --- | --- |
| probe | **loopback** server, 20 ms modelled latency, no gateway |
| the page | fetches through this sandbox's **egress proxy** |
| the proxy at 8-way, measured with curl (12 fetches) | **5.0 s serial, 2.0 s parallel - 2.5x** |

So the ceiling on this network is the gateway, not the client. The change is still correct and is
worth the probe's figure on a network without that gateway.

### The premise was wrong twice over

The row was written from a run that reported "188 linked resources". Tracing says otherwise:

| phase | cost |
| --- | --- |
| discover + fetch resources | **~47 s** (1,324 dependencies, 8 workers) |
| `create_scripted_document` | **~14.4 s** |
| paint | 68 ms |

Two corrections. **188 is how many resources load, not how many are asked for: the page discovers
1,324.** And the arithmetic that produced "188 x 300 ms = 57 s" was a coincidence - the phase costs
what it costs for 1,324 requests, not 188.

### How the wrong reading survived two tests

Removing the `<link>` tags changed nothing, and removing the `<script>` tags changed nothing, which
looked like proof that the fetches were not the cost. It was not: the dependencies come from the
stylesheet's `url()` references as much as from the markup, so stripping the tags left all 1,324 of
them in place. Both runs still printed "188 linked resources", which was the clue I read past.

The lesson is the one this row keeps repeating: a phase timing printed by the program is evidence, and
a variant of the input that "should" have removed the work is a hypothesis.

### What is left

Row **0.5**: 1,324 discovered against 188 loadable - 1,136 requests for resources that are not there,
at ~250 ms each through the proxy. The next step is a probe that prints all of them with their
resolved absolute URLs and where each came from, which tells duplicates and bad resolution apart.

The traces used here are kept behind `AETHERIS_TRACE_RENDER=1`.

## 5. Fifth commit — row 0.5: the engine was downloading the page's link graph

Row 0.5 said 1,324 dependencies were discovered for 188 loadable resources. `tools/dep-cost.cpp`
prints all of them with their resolved URL and whether the markup or the CSS named them, and the
answer was immediate:

| before | |
| --- | --- |
| discovered | **1,324** |
| distinct | 927 |
| duplicates | 397 over 140 URLs |
| the most repeated | `Google_Chrome` 47x, `Firefox_version_history` 21x, `Pale_Moon` 19x |

Those are **article hyperlinks**. `discover_dependencies` took every `href` or `src` in the document
text with one regex, so `<a href>` was a dependency and the engine downloaded the page's link graph.

### Fix one: walk tags, not text

Skip `href` on `<a>` and `<area>`; keep `src` anywhere and `href` on `<link>`. **1,324 -> 102**
discovered, 49 distinct.

### Fix two: the bug the first one was hiding

With the hyperlinks gone, the render reported **0 linked resources** - every remaining fetch failed.
The URLs were `load.php?lang=en&amp;modules=...`: attribute values were never HTML-decoded, so the
`&amp;` reached the wire and the page's own stylesheets had **never loaded at all**. The old 188
"successes" were the hyperlinks.

Decoding `&amp;`/`&lt;`/`&gt;`/`&quot;`/`&#39;` before resolving took it from 0 to **41 real
subresources** when the page is rendered from its URL.

### What this does not change

The render still looks unstyled: 41 stylesheets load now, but this engine cannot reproduce Vector
2022's CSS. The win is that the fetches are correct, not that the page looks like wikipedia yet.

### A harness fact worth keeping

Rendering `file://<local path>` makes every relative URL resolve to `file://`, so the corpus harness
fetches **none** of a page's subresources. That is row **0.6**, and it means every corpus measurement
before today exercised markup, CSS and layout with none of the page's own stylesheets, images or
scripts.

### The pod came back stale, again

Mid-session `git log` showed `fed64d9c` while `origin/main` was four commits ahead at `26a4ee55` -
the trap the project's own prompt lists. The work was saved as a patch, the tree reset to
`origin/main`, the patch re-applied (`git apply --check` first) and everything rebuilt before the
suite was run. The first suite run of the session had been against the stale tree and was thrown away.

## 6. Sixth commit — row 0.6: the corpus finally renders pages, not files

`tools/run-corpus.sh` rendered `file://<local path>`, which resolves every relative URL against
`file://`. So the corpus had never fetched a page's stylesheets, images or scripts - every measurement
in `docs/COVERAGE-CORPUS.md` before today tested markup, CSS and layout with none of the page's own
assets.

It now renders **from the URL**. The curl path stays as a fallback for a host the proxy refuses, and
the summary records which of the two ran.

| site | mode | subresources |
| --- | --- | --- |
| `example.com` | url | 0 (it has none) |
| `en.wikipedia.org/wiki/Web_browser` | url | **42** |
| `www.debian.org` | url | **26** |
| the other 17 | — | refused by the proxy |

### Two reporting faults found while doing it

1. **A refused host was reported as "rendered".** The proxy answers 502 with a 225-byte error body.
   The runner fetched it, wrote it to disk and rendered it as if it were a site, so 17 of 20 summary
   lines said "rendered" for hosts that never loaded. A non-2xx is now `refused`.
2. **The subresource count was not recorded**, and it is the number that separates a real render from
   a body of text. It is a column now.

Both are the same lesson as row 0.4's: a summary line that says "ok" is a claim, and a claim needs
the number behind it to be checked.

Row 0.1 remains blocked on the allowlist - 17 of 20 hosts still refused - but the tool is now honest
and, on a network that admits the corpus, would measure something real.

## 7. Seventh commit — row 0.5's tail: unfetchable schemes and `<base href>`

Row 0.5 closed with two named remainders. Both are done, and they are small because the diagnosis was
already precise.

### 47 of the 102 were unfetchable

A wikipedia page carries one `<link href="mw-data:TemplateStyles:r...">` per transcluded template
style. `mw-data:` is a Wikipedia-internal pseudo-scheme: no backend can serve it, so every one was
fetched and failed. Discovery now queues only a scheme a backend can serve - `http`, `https`, `file`,
`data` - which removed all 47.

### `<base href>` was ignored

Every relative URL was resolved against the request URL. A document that rebases itself was therefore
resolved against the wrong directory. The first `<base href>` in the document now becomes the
resolution base.

### Result

| | discovered |
| --- | --- |
| before row 0.5 | 1,324 |
| after skipping hyperlinks (0.5) | 102 |
| after the scheme filter and `<base href>` | **44** |

97% fewer requests, and the render still loads its **42** real subresources - the count is unchanged,
which is the check that matters: fewer requests, same resources.

### Where the plan stands

The checklist has no open rows. Row 0.1 stays blocked on the allowlist (3 of 20 hosts, unchanged
today). The other twelve rows carry named tails, which the convention keeps in the row that owns them
rather than as new rows; the highest-value ones still outstanding are remote `@font-face` sources
(3d1), CSP nonces and hashes (5a), transitions started by a style change (3d2), and persistence for
history and bookmarks (4.2).

## 8. Eighth commit — row 3d1's tail: fonts from `data:` and from the network

`@font-face` only ever worked for a file on disk. A page whose fonts arrived inline or over the network
drew every glyph in the engine's own face.

### The parser had to change first

`src: url(data:font/ttf;base64,AAAA)` produced **no face at all**, not even a bad one. The declaration
value scan ran to the first `;`, and the `;` in `data:font/ttf;base64` ended the declaration, leaving
`url(data:font/ttf` with no closing paren. The face had no source and was dropped.

The scan is paren-aware now - the same nested-syntax rule the selector scanners needed, applied to
declaration values. That is the third place in this engine where a `;` or `,` inside parentheses has
been read as a terminator.

### Then three sources, where there was one

| source | how |
| --- | --- |
| `data:` | base64 decoded inline; a percent-encoded `data:` URI is skipped rather than guessed at |
| file | as before |
| `http(s)` | looked up in the resource cache, which the CLI now populates |

The remote case needed the CLI: a `@font-face` source is named inside the stylesheet and never appears
in the document's dependency list, so nothing was fetching fonts. `run_render` now scans the assembled
CSS for `@font-face` sources and fetches the remote ones into the cache, where `RenderDocument` looks
them up.

A new `ResourceRequestContext::Font` maps to the `font-src` CSP directive. Borrowing `Image` would have
enforced `img-src` on fonts, and the fallthrough would have enforced `connect-src`.

### What the tests pin

`AetherisShaping` now asserts: a base64 `data:` URI registers the face and paint carries the family; a
percent-encoded `data:` URI is skipped; a face whose bytes are cached registers; an uncached remote face
is skipped rather than invented. The last two run without a network by seeding the cache directly.

Still open in this row: `font-weight`/`font-style` do not select among several faces of one family.

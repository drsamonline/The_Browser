# Coverage corpus — first run, 2026-11-23

Row `0.1` in `docs/CHECKLIST.md`. The row's purpose is to find out what breaks on real pages, in numbers, instead of
guessing. This is the first run and what it found.

## How it runs

```sh
tools/run-corpus.sh [output-dir] [list-file]     # tools/corpus-urls.txt holds the fixed list
```

Each site is fetched with `curl` and rendered by the engine from a local file. The fetch is curl's job rather than the
engine's for one reason: this sandbox reaches the internet through an **HTTP proxy**, and the engine's client connects
directly. That is a real gap in the engine — recorded below as its own row — but it is a networking gap, and this row
is about rendering.

`--render file:///…` works because the CLI now registers `FileResourceBackend` alongside the HTTP one; before this
commit a local file could not be rendered at all.

## The blocker, precisely

The sandbox's egress allowlist admits only a few hosts. Measured with `curl`:

| host | result |
| --- | --- |
| `example.com` | 200 |
| `en.wikipedia.org` | 301 → 200 |
| `www.python.org`, `news.ycombinator.com`, `text.npr.org`, `danluu.com`, `sqlite.org`, `www.gnu.org` | **502 — refused by the proxy** |

So the fixed list of twenty cannot be fetched here. The tool is committed and ready; the run needs an environment whose
network policy admits the corpus. The row stays blocked with that as its stated reason, which is more precise than the
"no open internet" it carried before.

## What the two reachable sites already showed

| site | bytes | result |
| --- | --- | --- |
| `example.com` | 577 | rendered |
| `en.wikipedia.org/wiki/Web_browser` | 416,975 | **did not finish in 280 s**, with scripts and without |

That second line is the finding. Two measurements narrowed it down.

### 1. Parse is fine; layout is not

| document | parse | style + layout |
| --- | --- | --- |
| wikipedia, first 8,339 bytes | 10 ms | 13 ms |
| wikipedia, first 20,848 bytes | 25 ms | **121,972 ms** |

Parse scales linearly. Layout does not: a 2.5× larger document took nine thousand times longer.

### 2. On synthetic pages, layout is linear but enormously slow

| elements | empty `<div>`s | `<div>`s with text | inline `<span>`s |
| --- | --- | --- | --- |
| 100 | 78 ms | 161 ms | 170 ms |
| 200 | 144 ms | 299 ms | 291 ms |
| 400 | 316 ms | 636 ms | 544 ms |

Roughly **0.8 ms per element**, and linear in element count. A browser lays out 400 small boxes in well under a
millisecond, so this is three orders of magnitude off — and the wikipedia number shows some real markup is far worse
than even that, which the synthetic shapes do not reproduce.

## Rows this run created

| row | what |
| --- | --- |
| `3j` | Layout costs ~0.8 ms per element on synthetic pages and explodes on real markup (20 KB of wikipedia: 122 s). Profile it, then fix the largest term. |
| `0.3` | The engine's HTTP client has no proxy support, so it cannot fetch through one. The corpus needs this to fetch its own pages. |

## What is not claimed

The corpus has not been run against twenty sites. Two sites were reachable, one of them rendered, and the other
produced the measurements above. Anything said about "real pages" in general is said from those two.

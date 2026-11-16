# The Aetheris browser family

Four browsers, one author, one contract. They are deliberately different sizes,
built on four different engines, and they agree on a small set of interchange
formats so that they can hand work to each other instead of pretending to be
each other.

| Tier | Repo | Engine | JavaScript | Idle RSS | Job |
|------|------|--------|-----------|----------|-----|
| T0 | [Aetheris](https://github.com/drsamonline/The_Browser) | own C++ engine, Ladybird lineage | yes | 200 MB+ | full-fidelity web, the reference oracle |
| T1 | [Nodus](https://github.com/drsamonline/nodus) | own C engine, NetSurf lineage | optional, Duktape | 25-70 MB | the core everything else can borrow from |
| T2 | [Sorex](https://github.com/drsamonline/sorex) | own C engine, Dillo lineage | never | 15-30 MB | the everyday GUI on weak hardware |
| T3 | [Volumen](https://github.com/drsamonline/volumen) | own C engine, Lynx lineage | never | 5-8 MB | text reading, dumping, automation |

GitHub topic for the whole group: [`aetheris-family`](https://github.com/topics/aetheris-family).

## Why four browsers and not one

A single browser has to choose one point on the fidelity-versus-cost curve. The
web does not have a single point: a documentation page, a login form, a
dashboard and a news article have wildly different requirements. Rather than
compete with Chromium on fidelity and lose on memory, the family competes on
memory and delegates fidelity upward.

The rule that keeps this from becoming four half-finished browsers is the
**ladder**: when a browser cannot render a page correctly, it does not grow a
new engine feature. It hands the URL to the next tier and tells the user why.

```
Volumen  --(needs images or CSS)-->  Sorex  --(needs JavaScript or modern CSS)-->  Aetheris
   |                                    |
   +----------- Nodus sits underneath both as a shared core --------------------+
```

## The Vinculum contract

*Vinculum* is the bond between the tiers: a specification, not a library. Every
repo carries a copy in `shared/vinculum/` and every copy must stay byte-identical.
It defines:

1. **`config.schema.json`** - one user configuration document, four readers.
   Same file in `~/.config/vinculum/config.json` for all four browsers.
2. **`capabilities.json`** - each browser declares what it can and cannot do, so
   the ladder can be negotiated by machine instead of by documentation.
3. **`handoff-protocol.md`** - how one tier passes a URL, a session or a scroll
   position to the next, and how the user is told it happened.
4. **`keybindings.toml`** - one keymap, honoured where the tier has a keyboard UI.
5. **`search-engines.json`** - one search engine list, no per-browser duplication.
6. **`session-format.md`** - session and tab interchange, so a session survives a
   handoff between tiers.
7. **`lite-profile.md`** - the rendering subset every tier must support, and the
   shared conformance corpus that measures it.

Anything a browser wants to add to Vinculum is proposed here first, in the repo
where it was needed, then copied to the other three in the same week. Drift is
the failure mode; the copies exist to make drift visible in a diff.

## The licence firewall (read this before sharing code)

This is the part that decides what can actually move between repos. It was
checked against the source headers on 2026-11-16, not assumed.

| Repo | Upstream | Licence as found in the tree |
|------|----------|------------------------------|
| Sorex | Dillo | GPL-3.0-**or-later** (`src/dilloc.c` header: "either version 3 ... or any later version") |
| Nodus | NetSurf | GPL-2.0-**only** (`content/content.c` header: "version 2 of the License", no "or later") plus an OpenSSL linking exception |
| Volumen | Lynx | GPL-2.0-**only** (verbatim GPLv2 `COPYING`, per-file terms via `COPYHEADER`; no blanket "or later" grant found) |
| Aetheris | Ladybird lineage | MIT |

Consequences, in order of how often they will bite:

1. **Nodus <-> Volumen is the only free corridor.** Both are GPL-2.0-only, so code
   may move in both directions with no relicensing. This is the family's real
   shared-code highway and the plan leans on it hard: Nodus's CSS work feeds
   Volumen's text flow, and Volumen's text extraction feeds Nodus's text frontend.
2. **Sorex is a cul-de-sac.** GPL-3.0 code cannot enter a GPL-2.0-only project,
   and GPL-2.0-only code cannot enter a GPL-3.0 project. Nothing moves into or out
   of Sorex except ideas, specs and data. Plan for reimplementation, not copying.
3. **Aetheris takes nothing from the other three except data and specs.** MIT is
   one-way compatible with GPL, not the reverse. Vinculum files are CC0-1.0 for
   exactly this reason, and Aetheris can adopt them without any licence question.
4. **The MIT helper libraries are the loophole, and it is a large one.**
   `libcss`, `libdom`, `libhubbub`, `libparserutils`, `libwapcaplet`, `libnsgif`,
   `libnsbmp` and `libsvgtiny` are MIT, separately maintained by the NetSurf
   project, and may be vendored into all four repos, including Aetheris. This is
   the single most valuable shared asset in the family: a complete HTML parser and
   CSS engine that costs nothing legally.

Nothing in this document is legal advice. If a contribution matters commercially,
have a lawyer read the specific file headers before merging.

## Shared conventions

- **Naming.** One Latin noun per repo, one English sentence in the README saying
  what it is. Binaries are named after the repo, lowercase.
- **Versioning.** `MAJOR.MINOR.PATCH` plus an upstream sync tag: Sorex
  `0.x.y+dillo.3.3.0`, Nodus `0.x.y+netsurf.3.11`, Volumen `0.x.y+lynx.2.9.3f`.
  The `+` tag is the exact upstream state the tree is derived from.
- **Upstream discipline.** Every repo keeps a read-only `upstream` git remote and
  syncs monthly. Local changes are either first-class files or documented patches
  in `PATCHES.md`; never silent edits to vendored code.
- **Measurement.** No performance claim is accepted without `tools/measure.sh`
  output pasted into the pull request. The family's shared unit is resident set
  size in megabytes, measured the same way in all four repos.
- **Provenance.** `SOURCES.md` names the exact upstream commit. A tree that cannot
  say where it came from is not part of this family.

## What is not in this family

`drsamonline/audit` is **not** a member. It is the fleet changelog — the record of what was measured,
what was fixed and the evidence for it, across every repository in the account. It carries no tier, no
Vinculum contract and no `aetheris-family` topic, and it should not be added to the group: a record
that is also a participant stops being a record.

The family is four browsers and a contract. Anything else that wants in has to justify itself against
that sentence.


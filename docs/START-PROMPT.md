# START PROMPT — Aetheris, resume (2026-11-17, second session)

Paste this as the first message of a new session. It is self-contained: it does not assume anything about
the sandbox, and it says where to read the rest from the repository.

---

You are resuming work on `drsamonline/The_Browser` ("Aetheris"), a C++20 browser engine derived from
Ladybird, MIT licensed. Work only in that repository. Documentation changes only in `sorex`, `nodus`,
`volumen`; exactly one entry per session in `drsamonline/audit`.

## Step 0 — establish state before touching anything

    git clone https://github.com/drsamonline/The_Browser.git && cd The_Browser
    git log --oneline -3
    grep -c "layout_passes\|key_checks\|laid_out_nodes" src/rendering/render_document.hpp

| Check | Expected |
| --- | --- |
| `main` tip | `0c01edf7` — `docs: An+B, and where the session stands` — or newer |
| `docs/SESSION-HANDOFF-2026-11-17.md` | present, 18 items; read it fully |
| `docs/HANDOVER.md` | present — build, gotchas, known limitations |
| `docs/UPSTREAM-FIRST.md` | present — the feature→file map |

If the tip is **older** than `0c01edf7`, stop and report: the branch has moved backwards.

Symbols that must exist before you edit anything (each is a thing a session added, and their absence means
the tree is older than this prompt):

| Symbol | Where | Means |
| --- | --- | --- |
| `layout_passes`, `key_checks`, `laid_out_nodes` | `render_document.hpp` | incremental layout, all three counters |
| `reuse_subtree`, `LayoutReuse` | `layout.hpp` | per-node layout reuse |
| `set_attribute`, `note_child_list_mutation_at` | `dom_mutation.hpp` | the attribute choke point, structural notes |
| `uses_positional_pseudo_class` | `css_stylesheet.hpp` | the invalidation list for positional selectors |
| `matches_an_plus_b` | `style.cpp` | `:nth-child(2n)` and friends |

## Step 1 — read, in this order

0. `docs/CHECKLIST.md` — the live progress list. **Read it first and update it in the same commit as any work.**
1. `docs/SESSION-HANDOFF-2026-11-17.md` — 21 items, in order, each with what shipped and the numbers.
2. `docs/HANDOVER.md` — build, the state table (one row per mechanism), known limitations.
3. `docs/UPSTREAM-FIRST.md` — **read upstream before writing any feature.**

Upstream reference:

    git clone --depth 1 --filter=blob:none --sparse https://github.com/LadybirdBrowser/ladybird.git
    cd ladybird && git sparse-checkout set Libraries/LibWeb

Layout line breaking is Rust, under `Libraries/LibWeb/Rust/src/css/` — `style/selector.rs` for selector
matching predicates, `style/component_matching.rs` for the position matcher, `parser/query_parser.rs` for
selector parsing. The C++ side is a bridge; the answers are often in the Rust.

## Step 2 — the first task

`docs/CHECKLIST.md` is the authority on what is open. `docs/PHASES.md` (section "Reviewed order, 2026-11-17")
says what order to take it in and which rows batch together. The handoff's `Next` list, for reference:

1. **`:is`, `:where`, `:has`.** Each needs a selector-list argument. `:has` also needs invalidation in the
   other direction — a child's change restyling an ancestor — which the current dirty sets do not do.
2. `[a=v i]` and sibling combinators (`+`, `~`) — **not built, by evidence**: no fixture, test page or
   stylesheet under `Tests/`, `docs/` or `src/` uses either. Revisit only when a page needs them. If you add
   `+`/`~`, `collect_state_change_dirty()` has a comment at the exact line that must change with them.
3. Family P0 builds — blocked in this sandbox (no `make`, `autoconf`, dev headers). Needs a machine with dev
   packages; record versions in each `SOURCES.md`.
4. `register_subtree()` / `purge_subtree()` no longer note a mutation — they only walk the tree for JS handles,
   and their **callers** note the child-list change. A new caller that forgets leaves the page stale.

## Environment

    pip install cmake ninja ziglang
    # wrappers: /scratch/work/toolchain/zigcc and zigcxx -> the zig binary, `cc` / `c++`

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_COMPILER=/scratch/work/toolchain/zigcc -DCMAKE_CXX_COMPILER=/scratch/work/toolchain/zigcxx \
      -DCMAKE_C_COMPILER_WORKS=1 -DCMAKE_CXX_COMPILER_WORKS=1 \
      -DAETHERIS_STANDALONE_BUILD=ON -DAETHERIS_ENABLE_TESTING=ON -DBUILD_TESTING=ON \
      -DOPENSSL_INCLUDE_DIR=/usr/include/node \
      -DOPENSSL_SSL_LIBRARY=/usr/lib/x86_64-linux-gnu/libssl.so.3 \
      -DOPENSSL_CRYPTO_LIBRARY=/usr/lib/x86_64-linux-gnu/libcrypto.so.3 \
      -DAETHERIS_WEBP_LIBRARY=/usr/lib/x86_64-linux-gnu/libwebp.so.7 \
      -DCMAKE_CXX_FLAGS="-Wno-error=#warnings"

**The last flag is not optional in this sandbox.** `zig cc` at `-Og` does not define `__OPTIMIZE__`, so glibc's
`_FORTIFY_SOURCE` warning becomes an error under `-Werror` on the first translation unit. Fortify is inert at
`-Og` either way; the flag stops it failing the build.

A clean build is three `cmake --build build --parallel` calls of ~280 s each. Configure alone is ~280 s. The
sandbox recycles: after a restore the tree may be **stale** — `git pull --ff-only` and check a symbol from the
table above before editing. `/scratch/work` does not survive a recycle: commit and push early.

## Verification habit

    ctest --test-dir build --no-tests=error -j 8      # 59 tests, ~95 s wall at -j 8

`AetherisWindowScripts` is ~80 s of that: it drives the real binary against loopback fixtures, about 40 runs.
It is the only test that renders anything. Do not delete parts of it for being slow.

`/tmp` is a **64 MB tmpfs** here. Frame dumps are 2.3 MB each. The window test clears `/tmp/aetheris-*-frames`
on the way out and writes its own dumps under the build tree; a second run from an uncleaned `/tmp` fails
elsewhere for a disk reason. Check `df -h /tmp` before believing a failure.

The window summary is the measurement surface:

    Window 1024x768 ..., relayouts: N, layouts: M, key checks: K, layout nodes: L, resolves: X, style hits: Y

| Counter | Means | A good number |
| --- | --- | --- |
| `relayouts` | rebuilds the window asked for | 1 per interaction |
| `layouts` | layout-engine passes | 1 = the load; a paint-only change adds none |
| `key checks` | layout keys compared in the last skip decision | 5 on a 406-node page after a hover |
| `layout nodes` | nodes actually laid out, text runs included | 5 of 405 for a text edit in row 100 |
| `resolves` | nodes whose style was re-resolved | 5 after a local change |
| `style hits` | nodes that reused a cached style | the rest of the page |

Extend this surface rather than opening a debugger. When a counter can go stale — `layout nodes` did after a
skipped pass — fix the counter before believing it.

## Working rules — earned, not decorative

1. **Read upstream first.** This session's clearest win was reading `CSSStyleDeclaration.idl` and *not* building
   named properties on `style`, because the IDL has no named-property accessor. Reading cost one command; the
   build would have cost a day and been wrong.
2. **Check the instrument before the subject.** Six failures this session, every one the instrument:
   - a full `/tmp` read as a bad render (`AetherisDomHost` "render exits 0");
   - `zig cc` + `_FORTIFY_SOURCE` read as a broken tree;
   - `flex-grow` "has no effect" — the fixture's children had no `height`, so every probe fell below them;
   - an `innerHTML` fixture with no element for its `#mid` id, read as "the reuse path did not fire";
   - `class='hit'` replacing `class='r'`, which grew a row and moved 200 rows — caught by the layout-node
     count, not the frame comparison;
   - an `:empty` control whose expected colour matched the failure colour, green for two commits.
3. **A control must be able to fail.** If the expected value equals what the broken code produces, the
   assertion proves nothing. Say what the failing case looks like before writing the assertion.
4. **Specificity is the fixture's most common lie.** Four times this session a rule "did not apply" because a
   fixture put `#id` against `.class` (10000 against 100). The tell is reliable: **relayout count right, pixel
   wrong — look at specificity.**
5. **Two probes, then park.** Record what was measured and eliminated, with no theory attached.
6. **Batch.** Several items per build; one audit entry per batch; one commit per item.
7. **Never ship a green test you cannot explain**, and never tune a test until it passes. If behaviour changed
   on purpose, say so in the commit message.
8. **State limits in the code and in `HANDOVER.md`**, not only in the commit message.
9. `git config user.name "Dr Sohil Momin"` and `user.email "sohilmomin2000@gmail.com"` in the clone — there is
   no global identity. Small self-contained commits to `main`. Never force-push or rewrite history. If a commit
   message turns out to be wrong, correct the record in `HANDOVER.md` and the audit entry and point at the
   commit; do not rewrite it.

## The invariant to keep in mind

Incremental layout reuses a subtree's geometry when nothing restyled inside it and it sits at the same
`(x, y, available_width)`. That is sound because a node's subtree geometry is a function of (its resolved style,
those three numbers, its DOM subtree). Anything that makes geometry depend on state outside that tuple breaks
it. `float` is not implemented and flex items take their width from the parent, so nothing does today — read
that sentence again before adding `float`.

## Audit entry

`drsamonline/audit`, `log/YYYY-MM/YYYY-MM-DD-<slug>.md`, headings: what I looked at upstream, what shipped,
evidence (raw numbers), what I deliberately did not do, next. Verify with the contents API. One entry per
session: extend it as the session goes rather than creating a second file.

## Answer style

Tables and bullets for status, never paragraphs. No emoji. Say plainly what is unverified.

# START PROMPT — Aetheris, resume

Paste this as the first message of a new session. It reads everything from the repository, so the
session resumes exactly where the last one stopped.

---

You are resuming work on `drsamonline/The_Browser` ("Aetheris"), a C++20 browser engine derived from
Ladybird, MIT licensed. Work only in that repository. Documentation changes only in `sorex`, `nodus`,
`volumen`; one entry per session in `drsamonline/audit`.

## Step 0 — clone and check state

    git clone https://github.com/drsamonline/The_Browser.git && cd The_Browser
    git log --oneline -5
    ls docs/

| Check | Expected |
| --- | --- |
| `main` tip | at least `c3695533` (session handoff committed) |
| `docs/SESSION-HANDOFF-2026-11-17.md` | present |
| `docs/HANDOVER.md` | present |
| `docs/UPSTREAM-FIRST.md` | present |

If the tip is older than `408c4c1a`, stop and report: the branch has moved backwards.

## Step 1 — read, in this order

1. `docs/SESSION-HANDOFF-2026-11-17.md` — where the last session stopped, including the work that was
   built and green but never committed.
2. `docs/HANDOVER.md` — build, gotchas, "check the instrument before the subject", known limitations.
3. `docs/UPSTREAM-FIRST.md` — the feature→file map. **Read upstream before writing any feature.**

Upstream reference:

    git clone --depth 1 --filter=blob:none --sparse https://github.com/LadybirdBrowser/ladybird.git
    cd ladybird && git sparse-checkout set Libraries/LibWeb

## Step 2 — resume at the first unfinished item

The handoff lists the next four items in order. The first is **incremental layout**, whose design was
applied, built and green (59/59) in the last session but never committed — the sandbox died. Treat it as
lost, re-apply it from the handoff's design section, and commit it.

If it is already on `main` (`layout_passes` exists in `src/rendering/render_document.hpp`), move to item
2: caching across interaction-state changes, using upstream's `ancestor_traversal_for_pseudo_class`.

The one trap, repeated here because it cost a build: in `js_set_attribute`, `note_dom_mutation_at(node)`
clears the recorded attribute name, so it must be called **before** `note_attribute_mutation(name)`.

## Working rules — earned, not decorative

1. **Read upstream first.** Port the shape (data flow, order of operations, names), not just the outcome.
2. **Check the instrument before the subject.** Three investigations blamed the engine and were wrong
   three times: a pixel sample read as a picture, a verdict string computed backwards, an assertion whose
   colour test also matched white. Print raw values, never a derived verdict. Test a colour as that colour
   (`r > 200 && g < 100 && b > 200` for magenta — `r > 200 && b > 200` also matches white). Look at the
   frame before reasoning from coordinates. Make the control case able to fail.
3. **Two probes, then park.** Record what was measured and eliminated, with no theory attached.
4. **Batch.** Several items per build; one audit entry per batch; one commit per item.
5. **Never ship a green test you cannot explain**, and never tune a test until it passes. If a test
   changes because behaviour changed on purpose, say which it is in the commit message.
6. **State limits in the code and in `HANDOVER.md`**, not only in the commit message.
7. `git config user.name "Dr Sohil Momin"` and `user.email "sohilmomin2000@gmail.com"` in the clone —
   there is no global identity. Small self-contained commits to `main`. Never force-push or rewrite.

## Environment

    pip install cmake ninja ziglang
    # wrappers: /scratch/work/toolchain/zigcc and zigcxx -> `zig cc` / `zig c++`

The full cmake invocation is in the handoff. A clean build is three `cmake --build build --parallel`
calls of ~280 s. The sandbox recycles often: a restored tree may be **stale** — `git pull --ff-only`
first, and confirm a symbol you expect exists (`set_focus`, `layout_passes`) before editing.
`/scratch/work` does not survive a recycle: commit early, push often.

## Verification habit

    ctest --test-dir build --no-tests=error -j 8      # 59 tests, ~20 s

The window summary is the measurement surface:

    Window 1024x768 ..., relayouts: N, layouts: M, resolves: X, style hits: Y

Extend it rather than opening a debugger.

## Audit entry

`drsamonline/audit`, `log/YYYY-MM/YYYY-MM-DD-<slug>.md`, headings: what I looked at upstream, what
shipped, evidence (raw numbers), what I deliberately did not do, next. Verify with the contents API.

## Answer style

Tables and bullets for status, never paragraphs. No emoji. Say plainly what is unverified.

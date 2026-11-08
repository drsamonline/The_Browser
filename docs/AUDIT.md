# Aetheris audit — 2026-11-08

**Author:** Dr. Sohil Momin · Part of the [Aetheris documentation set](USER_GUIDE.md)

An evidence-backed audit of the current state, followed by the plan forward.
Every finding below was produced by a command that was run, not by reading and
assuming. Commands and outputs are quoted.

## Verified healthy

| Check | Command | Result |
| --- | --- | --- |
| Test suite | `ctest --no-tests=error` | **51/51 passing** |
| Strict build | project `-Werror` set (`-Wcast-qual -Wformat=2 -Wmissing-declarations -Wmissing-field-initializers -Wsuggest-override`), headless config | `src/main.cpp`, `src/js/js.cpp`, `src/rendering/document.cpp` compile **clean** |
| Size profile | `ninja aetheris` (Release, `-Os`, LTO, stripped) | **2,515,056 bytes (2.40 MB)**, reported not enforced |
| Robustness | 6 hostile inputs (unclosed tags, 400-deep nesting, 20 KB attribute, broken entities, `</div>` inside a script, NUL bytes) | all `rc=0`, stderr clean |
| Debt markers | `grep TODO\|FIXME\|HACK` over `src include Tests` | **0** |
| CI workflows | YAML parse | `build-aetheris.yml` (4 jobs) and `deploy-demo.yml` valid |

The strict-build row matters: it closes the open risk from the previous session,
where `main.cpp` could not be verified under `-Werror` because `lz4.h` was
missing. The repository ships `cmake/fallback/include/lz4.h`, and the headless
UI path avoids X11 headers, so the check is now possible and passes.

## Findings

Ordered by severity. Each names the file and line.

### P1 — release integrity

**1. ~~The version string misrepresents the build.~~ — FIXED in batch 1.**
`--version` and the banner now report **v0.3.0**, and `CHANGELOG.md` has a `v0.3`
section covering QuickJS, gumbo, events, timers and the DOM surface.

### P2 — correctness and API gaps

**2. ~~`removeEventListener` does not exist.~~ — FIXED in batch 1.**
`removeEventListener` is available on window, document and elements, and adding
the same `(type, listener, capture)` twice is now a no-op, so listener arrays
cannot grow without bound. Covered by a test that adds a listener twice, checks
it fires once, removes it, and checks it stops firing.

**3. ~~`element.dataset` is read-only and silent about it.~~ — FIXED in batch 1.**
`dataset` is now a Proxy over the element: reads, writes and new keys all work,
and a write lands in the `data-*` attribute (`dataset.newKey = 'v'` creates
`data-new-key`). Covered end to end.

**4. `children` and `parentNode` allocate on every access.**
`js_children_get` builds a new array per read. That is correct and fixes the
earlier staleness bug, but `for (var i = 0; i < el.children.length; i++)` is now
O(n²). Worth a doc note or a cached array invalidated on mutation.

### P3 — architecture

**5. ~~Two parallel resource-loading paths.~~ — FIXED in batch 2.**
`SecureResourceLoader`, `HttpResourceBackend`, `FileResourceBackend`,
`DocumentRuntime` and `BrowserChrome` are compiled into `AetherisRendering` and
covered by tests (`TestSecurityPolicy`, `TestResourceBackends`,
`TestHttpState`, `TestDynamicRuntime`, `TestBrowserChrome`, `TestReleaseSmoke`),
but the `aetheris` binary never uses them — `src/main.cpp` fetches through
`HttpClient` directly. So the tested security policy and content-type handling
are not on the path a real page takes. The binary now fetches through `SecureResourceLoader`
(via `HttpResourceBackend`) for the document and every subresource, so the
same-origin, mixed-content, scheme and content-type policy the tests exercise is
the one a real page gets. A resource with an unsupported scheme (e.g. `ftp://`)
is refused by the policy and skipped.

### P4 — documentation

**6. ~~`docs/USER_GUIDE.md:29` claims "39 regression test executables".~~ — FIXED.**
Now states 51.

### P5 — housekeeping

**7. Size drifted +24,640 bytes (+24 KiB)** since the recorded reference
(2,490,416 → 2,515,056). Expected from the Phase A additions; the report
flagged it without failing the build, which is the intended behaviour. Update
the reference when the growth is deliberate.

## Plan forward

### Hygiene batch H (before Phase B) — small, and it clears the P1/P2/P4 findings

| Item | Fix |
| --- | --- |
| H1 | Bump to **0.3.0**: `src/main.cpp` version strings, a `v0.3` CHANGELOG section covering QuickJS, gumbo, events, timers and the DOM surface. |
| H2 | Add `removeEventListener`, and de-duplicate identical `(type, listener, capture)` registrations. |
| H3 | Either make `dataset` writable (setter writing `data-*`) or document it as a read-only snapshot. Prefer writable — it is a few lines. |
| H4 | Fix the stale test count in `docs/USER_GUIDE.md`. |
| H5 | Decide on finding 5: wire `SecureResourceLoader` into the binary, or document it as library-only. |

### Then the existing plan

Phase B (forms) → C (CSS breadth) → D (rAF) → E (vector fonts) → F
(network/security) → G (UI shell), per [PHASES.md](PHASES.md).

H5 is the one that should not be deferred indefinitely: a security policy that
is tested but not enforced on the real fetch path is worse than none, because it
reads as protection that is not there.

## Verdict

**Not a release blocker for development, but a release blocker for tagging
v0.3.** The code is in good shape — clean strict build, 51 green tests, clean
handling of hostile input — and the gap is bookkeeping plus two small API holes.
H1–H4 are a single short batch; H5 is a decision.

---

*Aetheris Browser — audit. Authored and maintained by **Dr. Sohil Momin**.*

# Aetheris roadmap and delivery workflow

**Author:** Dr. Sohil Momin · Part of the [Aetheris documentation set](USER_GUIDE.md)

This document defines *how* the remaining work is delivered, not just *what*:
the phases, the batches within them, and the workflow rules that keep the
time-to-quality ratio high.

## Where we are

- **v0.1 milestone:** met (see [V0.1_ACCEPTANCE.md](V0.1_ACCEPTANCE.md)). Build,
  CTest (49/49), smoke test and the size profile all pass; navigation, history,
  tabs, security-policy and attribution are in place.
- **Beyond v0.1:** the browser-engine surface is broad but not standards-complete.
  The phases below close the highest-value gaps first.

## Workflow rules (the "minimum time, maximum quality" part)

1. **Batch by shared code path, not by feature name.** Anything that touches the
   same file or the same subsystem lands in one batch, so it needs one build and
   one test cycle instead of many.
2. **One verification cycle per batch.** Build → `ctest` → one end-to-end harness
   run. A feature is not "done" until it is exercised through the real pipeline,
   not only a unit test.
3. **One push per phase.** Batches are committed locally as they finish; the
   phase is pushed to `main` as a small number of atomic commits, and one patch
   file per phase is produced. This cuts round-trips and keeps history clean.
4. **Fast inner loop.** Use `aetheris --eval "<script>"` for sub-second
   JavaScript iteration instead of a full rebuild-and-render.
5. **Warm the build.** Configure once, then incremental `cmake --build` only.
   Reconfigure only when CMake inputs change.
6. **Prefer vetted libraries for big, risky subsystems.** Crypto and codecs are
   integrated (OpenSSL, libwebp), not hand-rolled; the build degrades gracefully
   when they are absent (`AETHERIS_HAS_TLS`, `AETHERIS_HAS_WEBP`).
7. **Reuse upstream.** Ladybird/SerenityOS source in the tree is the reference
   for semantics; attribution is recorded in [CREDITS.md](../CREDITS.md).
8. **Delegate research, keep implementation.** Upstream API/algorithm lookups can
   be delegated to a sub-agent so they do not consume the main context.
9. **Never overclaim.** Every capability is backed by a test or a verified render;
   docs state known limitations.

## Phases

### Phase 0 — v0.1 release pass (½ batch)

- Run the exact acceptance commands from [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md)
  (`cmake --preset StandaloneSize`, build, smoke, `ctest`), record the release
  evidence, and tag the release.
- **Verify:** size-profile build + smoke; full CTest.
- **Effort:** small. **Value:** closes the milestone.

### Phase 1 — JavaScript engine (3 combined batches)

The engine is a tree-walking interpreter in `src/js/` with a small standard
library. Remaining gaps, ordered by value:

- **Batch 1a — Regular expressions (combined).** One small regex engine, used by
  `RegExp`, `String.match`, `test`, `replace(regex)`, `split(regex)` together.
  All of these share the engine, so they land in one batch and one test cycle.
- **Batch 1b — Async (combined).** A microtask queue plus `Promise`
  (`then`/`catch`/`resolve`/`all`) and `async`/`await` (parser + interpreter).
  They share the queue, so they ship together.
- **Batch 1c — Library breadth + element events (combined).** `Date`, `Map`/`Set`,
  remaining `String`/`Array`/`Math` methods, and element-level events
  (`element.onclick`, `element.addEventListener`) with host dispatch.
- **Verify (each batch):** `ctest` + `--eval` demo + one rendered-page check.

### Phase 2 — DOM and rendering completeness (2 combined batches)

- **Batch 2a — Hit-testing and element events.** Reuse the existing visual
  hit-testing so an event can be dispatched to the element under a point; this is
  what makes `onclick` real.
- **Batch 2b — CSS/HTML gaps (combined).** `transform`, remaining `grid`/`flex`
  behaviour, and additional HTML elements — grouped because they touch the same
  style/layout code.
- **Verify:** `ctest` + a rendered sample per batch.

### Phase 3 — Networking completeness (1–2 batches)

- Cookies, redirects, cache headers and HTTP/1.1 keep-alive, combined where they
  share the request/response path.
- **Verify:** loopback-server tests (the pattern already used for HTTP and TLS).

### Phase 4 — Toolchain-gated (a decision, not a batch)

Building the upstream **LibJS/LibWeb** wholesale needs a Rust toolchain, ICU dev
headers, and a clang GC plugin. This is a separate, multi-session effort and a
deliberate dependency decision — not part of the batched engine work above.

## Definition of done

A phase is done when: the batches are implemented, the CTest suite passes, an
end-to-end check demonstrates the behaviour, the docs and changelog state the
capability and its limitations, and the phase is pushed with one patch file.

---

*Aetheris Browser — roadmap. Authored and maintained by **Dr. Sohil Momin**.*

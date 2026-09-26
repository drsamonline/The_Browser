# Aetheris Architecture

**Author:** Dr. Sohil Momin · **Status:** current as of v0.1 candidate

This document describes how the Aetheris-specific code is organized and how it
relates to the upstream-derived tree. For upstream internals (LibWeb, LibJS,
RequestServer, etc.) see [`Documentation/`](../Documentation/README.md).

## Two codebases, one repository

```text
┌─────────────────────────────────────────────────────────────┐
│ Aetheris-specific (src/rendering, Tests/AetherisRendering)  │
│   Standalone C++20, zero third-party deps, own CMake target│
├─────────────────────────────────────────────────────────────┤
│ Upstream-derived (AK, Libraries, Services, UI, Meta, Base)  │
│   Ladybird/SerenityOS infrastructure, original licenses     │
└─────────────────────────────────────────────────────────────┘
```

The two halves are deliberately separated: `AetherisRendering` links against
nothing from the upstream tree, so it can be built, tested, and reasoned about
independently.

## Aetheris rendering pipeline

```text
HTML bytes ─► html_tokenizer ─► html_tree_builder ─► dom / document
                                                        │
CSS text  ─► css_tokenizer ─► css_parser ─► stylesheet  │
                                    │                   ▼
                              style.cpp ◄──── StyleResolver (match + cascade)
                                                │ computed styles
                                                ▼
                          layout.cpp (block / flex / grid, geometry, viewport)
                                                │ render_tree
                                                ▼
                    paint_executor + clip + visual_state (paint commands)
                                                │
                                                ▼
                            software_surface (rasterized output)
```

Supporting subsystems around the pipeline:

| Module | Responsibility |
|--------|----------------|
| `resource.{hpp,cpp}`, `resource_loader`, `resource_backend`, `file_resource_backend`, `http_resource_backend` | Resource model and pluggable fetch backends. |
| `secure_resource_loader`, `security_policy`, `content_type`, `cookie_jar`, `url` | Same-origin/mixed-content foundations, content typing, cookie state. |
| `navigation`, `page_lifecycle`, `document_runtime`, `form_runtime`, `document_interaction`, `dynamic runtime` pieces | Navigation results, lifecycle states, interaction and form behavior. |
| `browser_session`, `browser_application`, `browser_chrome`, `visual_state`, `viewport` | Tab/session state, multi-tab chrome, hit-testing and scrolling. |
| `font`, `text_layout`, `image`, `image_output`, `color`, `geometry`, `clip` | Typography, measurement, color/geometry primitives. |

Every module above has regression coverage under `Tests/AetherisRendering/`
(registered with CTest).

## Build integration

- Root `CMakeLists.txt` adds `src/` after `AK`/`Libraries`; `src/CMakeLists.txt`
  defines the `AetherisRendering` static library with `cxx_std_20` and the
  `AETHERIS_RENDERING_FOUNDATION=1` definition.
- Tests are added through `Tests/AetherisRendering/CMakeLists.txt` when
  `AETHERIS_ENABLE_TESTING` and `BUILD_TESTING` are on.

## Dormant prototype cluster

`src/main.cpp`, `src/cache/`, `src/network/`, `src/ui/`, and
`include/aetheris/` contain early experiments (ghost cache with LZ4-style
compression, socket abstraction, X11 window manager). They are **not** part of
any build target today because their external dependencies (LZ4, X11) are not
declared in `vcpkg.json`. Options going forward (tracked in
[ISSUES.md](../ISSUES.md)): declare the dependencies and add proper targets,
re-implement dependency-free, or remove.

## Design principles

1. Preserve working upstream architecture; justify every deviation.
2. Prefer measurable engineering over marketing claims (see README
   “Performance and benchmarking”).
3. New behavior lands with regression tests.
4. Upstream copyright and license notices stay intact
   ([UPSTREAM_ATTRIBUTION.md](UPSTREAM_ATTRIBUTION.md)).

---

*Aetheris Browser — architecture reference. Authored and maintained by Dr. Sohil Momin.*

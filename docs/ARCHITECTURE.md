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
| `src/js/` (`AetherisJS`) | Self-contained mini JavaScript engine: lexer, parser and tree-walking interpreter with a small built-in library. |
| `font`, `bitmap_font`, `text_layout`, `image`, `png`, `jpeg`, `gif`, `svg`, `webp`, `image_output`, `color`, `geometry`, `clip` | Typography (embedded 8x8 bitmap font + measurement), PNG/JPEG/GIF/SVG/WebP image decoding, colour and geometry primitives. |

Every module above has regression coverage under `Tests/AetherisRendering/`
(registered with CTest).

## Build integration

- Root `CMakeLists.txt` adds `src/` after `AK`/`Libraries`; `src/CMakeLists.txt`
  defines the `AetherisRendering` static library with `cxx_std_20` and the
  `AETHERIS_RENDERING_FOUNDATION=1` definition, the `AetherisRuntime` static
  library (the prototype cluster), and the `aetheris` executable.
- Tests are added through `Tests/AetherisRendering/CMakeLists.txt` when
  `AETHERIS_ENABLE_TESTING` and `BUILD_TESTING` are on.
- Two optional build profiles keep the tree buildable on bare runners and
  small on disk:
  - `AETHERIS_STANDALONE_BUILD=ON` builds only the Aetheris targets in `src/`
    plus the `Tests/AetherisRendering` suite, with no vcpkg toolchain.
  - `AETHERIS_SIZE_OPTIMIZED=ON` builds the Aetheris targets for minimum
    footprint (`-Os`, dead-section elimination, LTO, stripped executable).
  Both are exposed as CMake presets (`Standalone`, `StandaloneSize`).

## Prototype runtime cluster

`src/main.cpp`, `src/cache/`, `src/network/`, `src/ui/`, and `include/aetheris/`
contain the prototype runtime (ghost cache with LZ4 compression, socket
abstraction with DNS + a bounded connect timeout, an HTTP/1.1 client, and the
X11/Win32/Cocoa window manager). They are wired into the
`AetherisRuntime` static library and the `aetheris` executable, so they are
compile-checked in every standalone build. Their external dependencies (LZ4,
X11) are **optional**: `cmake/AetherisStandaloneDeps.cmake` falls back to a
bundled compression shim and a headless window manager when the system
libraries are absent, which keeps the standalone profile buildable on bare
runners. See [ISSUES.md](../ISSUES.md) for the remaining gaps in this cluster.

## Design principles

1. Preserve working upstream architecture; justify every deviation.
2. Prefer measurable engineering over marketing claims (see README
   “Performance and benchmarking”).
3. New behavior lands with regression tests.
4. Upstream copyright and license notices stay intact
   ([UPSTREAM_ATTRIBUTION.md](UPSTREAM_ATTRIBUTION.md)).

---

*Aetheris Browser — architecture reference. Authored and maintained by Dr. Sohil Momin.*

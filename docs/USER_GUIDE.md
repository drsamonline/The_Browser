# Aetheris User Guide

**Author:** Dr. Sohil Momin · **Version:** 0.1 (experimental) · See [CHANGELOG.md](../CHANGELOG.md)

This guide explains how to build, run, test, and navigate the Aetheris codebase,
and which parts of the tree are Aetheris-specific versus upstream-derived.

> Aetheris is an experimental project derived from the Ladybird/SerenityOS
> ecosystem. It is **not** a production-ready browser. Do not use it to browse
> untrusted networks. See [docs/V0.1_ACCEPTANCE.md](V0.1_ACCEPTANCE.md) for the
> honest capability list.

## Table of contents

1. [Repository map](#repository-map)
2. [Prerequisites](#prerequisites)
3. [Building](#building)
4. [Running tests](#running-tests)
5. [Using the Aetheris rendering foundation](#using-the-aetheris-rendering-foundation)
6. [Documentation index](#documentation-index)
7. [Reporting problems](#reporting-problems)
8. [FAQ](#faq)

## Repository map

| Path | Provenance | What it contains |
|------|------------|------------------|
| `src/rendering/` | **Aetheris** | The standalone C++20 rendering foundation: HTML tokenizer, tree builder, DOM, CSS tokenizer/parser, style resolution, layout (block/flex/grid), paint executor, software surface, resource loading, security policy, cookie jar, navigation, sessions, tabs, and browser chrome. Built as the static library `AetherisRendering`. |
| `Tests/AetherisRendering/` | **Aetheris** | 40+ regression test executables covering every subsystem above, wired into CTest. |
| `docs/` | **Aetheris** | Project-local documentation (this guide, development workflow, release process, provenance policy). |
| `AK/`, `Libraries/`, `Services/`, `UI/`, `Meta/`, `Utilities/`, `Base/` | Upstream-derived | Ladybird/SerenityOS infrastructure: core libraries, JS engine, image codecs, services, platform UI shells, build tooling, fuzzers, linters. |
| `Documentation/` | Upstream-derived | Original Ladybird developer documentation (build instructions, coding style, patterns, DevTools, etc.). Kept intact for reference. |
| `include/aetheris/`, `src/cache/`, `src/network/`, `src/ui/`, `src/main.cpp` | **Aetheris prototype (dormant)** | Early prototype experiments. Not wired into any default build target and pending dependency decisions (LZ4, X11). Treat as design reference only; see [ISSUES.md](../ISSUES.md). |

## Prerequisites

- CMake ≥ 3.25, a C++20 compiler (MSVC 2022 / Clang 17+ / GCC 13+).
- For the full upstream tree: vcpkg (`VCPKG_ROOT` set) and system packages
  reported by CMake's dependency check. See
  [BUILD_GUIDE.md](../BUILD_GUIDE.md) and
  [Documentation/BuildInstructionsLadybird.md](../Documentation/BuildInstructionsLadybird.md).
- The `AetherisRendering` library and its tests need only a C++20 compiler —
  no third-party dependencies.

## Building

```bash
cmake -S . -B build
cmake --build build --config Debug
```

On Windows, follow [docs/WINDOWS_BUILD.md](WINDOWS_BUILD.md) (linker modes
`AUTO` / `SYSTEM` / `LLD` via `-DAETHERIS_WINDOWS_LINKER_MODE=...`).

Reconfigure whenever CMake files or build-registered sources change:

```bash
rm -rf build && cmake -S . -B build   # clean reconfiguration
```

## Running tests

```bash
ctest --test-dir build --output-on-failure            # everything
ctest --test-dir build -R Aetheris --output-on-failure # Aetheris suite only
```

The Aetheris suite includes tokenizer, DOM, CSS, cascade, box-model, layout,
text, painting, resource, security, session/chrome, and release-smoke tests.

## Using the Aetheris rendering foundation

`AetherisRendering` exposes small, dependency-free classes in the
`aetheris::rendering` namespace. The pipeline pieces compose like this:

```cpp
#include "document.hpp"          // Document::parse_html -> DOM document
#include "css_parser.hpp"        // parse CSS text -> CssStyleSheet
#include "style.hpp"             // StyleResolver (selector matching + cascade)
#include "layout.hpp"            // LayoutEngine / LayoutTreeBuilder
#include "paint_executor.hpp"    // paint command generation
#include "software_surface.hpp"  // rasterization target

auto document = aetheris::rendering::Document::parse_html(html_source);
auto stylesheet = /* aetheris::rendering CSS parser entry point, see css_parser.hpp */;
aetheris::rendering::StyleResolver resolver;
auto computed = resolver.resolve(node, stylesheet);   // per-element style
// Build layout with LayoutTreeBuilder/LayoutEngine, then emit paint commands
// through PaintExecutor onto a SoftwareSurface.
```

The exact signatures evolve quickly — `Tests/AetherisRendering/Test*.cpp` are
the authoritative, always-compiling usage examples for every API above.

For tabbed browsing state, use `BrowserSession` (navigation, history, scroll,
hit-testing: `navigate()`, `back()`, `forward()`, `reload()`, `scroll_by()`,
`hit_test()`) and `BrowserChrome` (multi-tab address-bar state: `new_tab()`,
`tab_count()`, `set_address()`, `can_go_back()`, `can_go_forward()`).

## Documentation index

### Aetheris project docs (`docs/`)

| Document | Purpose |
|----------|---------|
| [USER_GUIDE.md](USER_GUIDE.md) | This guide. |
| [DEVELOPMENT.md](DEVELOPMENT.md) | Local validation workflow. |
| [WINDOWS_BUILD.md](WINDOWS_BUILD.md) | Windows toolchain and linker policy. |
| [RENDERING_ENGINE_PLAN.md](RENDERING_ENGINE_PLAN.md) | Historical implementation plan (now largely superseded by shipped code). |
| [UPSTREAM_ATTRIBUTION.md](UPSTREAM_ATTRIBUTION.md) | Provenance and licensing policy. |
| [V0.1_ACCEPTANCE.md](V0.1_ACCEPTANCE.md) | Release acceptance criteria and non-goals. |
| [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md) | Step-by-step release procedure. |

Root-level: [README.md](../README.md) · [CONTRIBUTING.md](../CONTRIBUTING.md) ·
[SECURITY.md](../SECURITY.md) · [ISSUES.md](../ISSUES.md) ·
[CHANGELOG.md](../CHANGELOG.md) · [CODE_OF_CONDUCT.md](../CODE_OF_CONDUCT.md) ·
[BUILD_GUIDE.md](../BUILD_GUIDE.md) · [DEVELOPMENT_WORKFLOW.md](../DEVELOPMENT_WORKFLOW.md) ·
[AUTHORS](../AUTHORS)

### Upstream developer docs (`Documentation/`)

Full index in [Documentation/README.md](../Documentation/README.md): build
instructions, advanced build options, testing, profiling, troubleshooting,
coding style, code policy, patterns, event loop, smart pointers, string
formatting, LibWeb internals, CSS property/generated-file machinery, media
pipeline, process architecture, DevTools, editor configuration, and porting.

## Reporting problems

Use the reduced-test-case workflow in [ISSUES.md](../ISSUES.md) and include
the release-evidence fields listed in
[docs/V0.1_ACCEPTANCE.md](V0.1_ACCEPTANCE.md) (OS, compiler, generator,
configuration, commit id).

## FAQ

**Is Aetheris a complete browser?** No. It is an experimental rendering
foundation plus an upstream-derived tree; see the acceptance-criteria
non-goals before drawing conclusions.

**Can I relicense the repository?** No blanket relicensing without the
file-level audit required by [docs/UPSTREAM_ATTRIBUTION.md](UPSTREAM_ATTRIBUTION.md).

**Why do `src/main.cpp` and `include/aetheris/` not build?** They are dormant
prototype code with unresolved external dependencies (LZ4, X11) and are not
part of any build target. Either wire them up properly or remove them; see
[ISSUES.md](../ISSUES.md).

---

*Aetheris Browser — user guide. Authored and maintained by Dr. Sohil Momin.*

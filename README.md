# Aetheris Browser

Aetheris Browser is an experimental C++ browser project built on a codebase derived from the Ladybird and SerenityOS browser ecosystem. The repository contains upstream-derived infrastructure, including components and libraries from that ecosystem, alongside Aetheris-specific experiments and modifications.

Aetheris is **not** presented as a browser built entirely from scratch. Its work focuses on adapting, extending, simplifying, and experimentally developing browser subsystems on an existing technical foundation.

> **Status:** Experimental. Aetheris is not currently a production-ready, standards-complete replacement for established browsers.

## Goals

- Explore a smaller and more understandable browser architecture where practical.
- Improve memory efficiency and startup characteristics through measurable engineering work.
- Develop HTML, CSS, DOM, layout, rendering, resource, and document integration incrementally.
- Preserve upstream licensing and attribution requirements.
- Prefer coherent, testable changes over speculative architectural rewrites.

The project aims to be more resource-conscious than typical Chromium- or Electron-based applications where practical and measurable. These are engineering goals, not guaranteed universal metrics.

## Upstream foundation and provenance

Aetheris contains code and infrastructure derived from the Ladybird/SerenityOS ecosystem. This includes foundational components visible in the repository such as `AK`, `LibCore`, and other imported browser infrastructure.

Aetheris-specific code should be distinguished from upstream-derived code during maintenance and review. Existing upstream copyright notices and licenses must not be removed or replaced by project rebranding.

See [`docs/UPSTREAM_ATTRIBUTION.md`](docs/UPSTREAM_ATTRIBUTION.md) for the repository-level provenance policy.

## Current architecture

```text
HTML input ──► HTML tokenization ──► DOM
                                      │
CSS input ───► CSS parsing ──────────┤
                                      ▼
                                Style resolution
                                      │
                                      ▼
                                    Layout
                         ┌────────────┼────────────┐
                         ▼            ▼            ▼
                       Block        Flex         Grid
                                      │
                                      ▼
                                 Render tree
                                      │
                                      ▼
                                 Paint commands
                                      │
                                      ▼
                                Paint execution
                                      │
                                      ▼
                                Software surface
```

The rendering pipeline currently includes work in block and inline layout, Flex and Grid foundations, positioning, sizing constraints, text layout, clipping, paint ordering, backgrounds, images, fonts, and software rasterization. Feature presence does not imply complete HTML or CSS standards compliance.

## Architectural optimization goals

### Resource and memory management

Aetheris may evaluate standard techniques such as controlled caches, allocation reduction, compressed resource storage, and secure clearing of sensitive memory where justified. LZ4 or similar mechanisms are implementation options rather than proprietary technology claims.

### Selective subsystem initialization

The project may explore delayed initialization of expensive subsystems, including scripting, when document requirements permit. Dynamic documents and compatibility requirements make this a design problem that must be validated rather than assumed to provide a universal benefit.

### Rendering efficiency

Current and future work may reduce redundant paint operations, improve clipping and invalidation, and evaluate accelerated backends where the complexity and dependencies are justified.

## Performance and benchmarking

Aetheris does not currently claim fixed limits for binary size, idle memory usage, or startup time.

Performance measurements should record the operating system, hardware, compiler, build configuration, enabled features, workload, and measurement method. Future public performance claims should be reproducible.

## Build and test

A typical development workflow is:

```bash
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build --output-on-failure
```

Reconfiguration is required when CMake configuration or build-registered files change. Otherwise, an existing build directory can be reused.

## Development principles

- Preserve working architecture unless a change has a clear technical justification.
- Do not claim code provenance, performance, security, or compatibility that has not been verified.
- Keep upstream attribution and licensing intact.
- Add regression tests with new behavior where practical.
- Prefer incremental, coherent changes and avoid unnecessary abstractions.
- Use clear subsystem-oriented commit messages.

## Project status

Aetheris is an experimental browser-engine and systems-programming project. It should not currently be represented as a complete browser built from an empty codebase, a fully standards-compliant browser, or a project with guaranteed resource limits unless those statements are supported by reproducible evidence.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md). In particular, contributors must determine whether affected code is upstream-derived or Aetheris-specific, preserve applicable notices, and validate relevant builds and tests.

## Licensing

Licensing and redistribution obligations must remain compatible with the upstream-derived code present in this repository. The existing repository license should not be treated as sufficient proof that all imported components may be relicensed under the same terms.

See [`docs/UPSTREAM_ATTRIBUTION.md`](docs/UPSTREAM_ATTRIBUTION.md). A dedicated provenance and licensing audit is required before making broad relicensing or commercial-use claims about the entire repository.

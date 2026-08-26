# Aetheris Browser build guide

Aetheris is built on a Ladybird/SerenityOS-derived codebase. Build requirements depend on the enabled upstream components and platform. Do not assume that a small generic dependency list is sufficient for every configuration.

## Standard development build

```bash
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build --output-on-failure
```

## Linux dependencies

Use the repository's CMake dependency checks as the source of truth. If configuration reports a missing package, install the development package named by CMake and rerun configuration. This is preferable to maintaining an unverified hard-coded package list.

## Windows

Use a current Visual Studio C++ toolchain and run CMake from a Developer PowerShell or equivalent configured environment. Reconfigure after changing CMake files.

## Reproducibility

Record compiler version, generator, build type, platform, enabled options, and dependency versions when reporting build failures or performance measurements.

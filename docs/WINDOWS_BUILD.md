# Building Aetheris on Windows

Aetheris is derived from the Ladybird codebase and retains its CMake-based build infrastructure. The project can be configured on Windows with either a standard MSVC toolchain or a Clang/LLVM-based toolchain, depending on the components enabled and the linker policy selected.

## Recommended MSVC Build Tools path

Install:

- Visual Studio 2022 Build Tools
- MSVC C++ x64/x86 build tools
- A Windows 10 or Windows 11 SDK
- CMake

Open a Visual Studio 2022 Developer Command Prompt for x64 and verify:

```cmd
where cl
where link
cl
```

From the repository root:

```cmd
if exist build rmdir /s /q build
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DAETHERIS_WINDOWS_LINKER_MODE=SYSTEM
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

`SYSTEM` tells Aetheris not to override the linker selected by the active Windows toolchain. For a standard MSVC Developer Command Prompt, this means the normal MSVC `link.exe` is used.

## Windows linker policy

`AETHERIS_WINDOWS_LINKER_MODE` accepts three values:

- `AUTO` — use `lld-link` when it is available; otherwise use the linker selected by the active toolchain.
- `SYSTEM` — always use the linker selected by the active toolchain.
- `LLD` — require `lld-link`; configuration fails with a clear error if it is unavailable.

Examples:

```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DAETHERIS_WINDOWS_LINKER_MODE=AUTO
```

```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DAETHERIS_WINDOWS_LINKER_MODE=SYSTEM
```

## LLVM / Clang path

If building with the inherited Clang-oriented configuration, install a compatible LLVM toolchain that provides `clang` or `clang-cl` and, when using the `LLD` linker mode, `lld-link`.

Verify the tools before configuring:

```cmd
where clang-cl
where lld-link
```

Then select the intended compiler and linker policy explicitly when configuring CMake.

## Clean reconfiguration

CMake caches compiler and linker decisions. After changing compilers, generators, SDKs, or linker modes, remove the existing build directory before configuring again:

```cmd
if exist build rmdir /s /q build
```

Do not commit the generated `build` directory.

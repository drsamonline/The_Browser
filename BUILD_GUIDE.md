# AETHERIS BROWSER - CROSS-PLATFORM BUILD GUIDE

## Hyper-optimized, Zero-bloat Browser
**Version:** 1.0.0  
**License:** MIT  
**C++ Standard:** C++20

---

## PREREQUISITES

### Windows (Visual Studio 2022)
- Visual Studio 2022 Community/Professional/Enterprise
- Desktop development with C++ workload
- CMake 3.20+ (included with VS 2022 or install separately)
- Git for Windows
- PowerShell (included with Windows)

### Linux (Ubuntu/Debian)
```bash
sudo apt update
sudo apt install -y build-essential cmake git pkg-config libx11-dev
```

### macOS
```bash
# Install Homebrew if not present
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install cmake git
```

---

## STEP-BY-STEP COMPILATION

### A) WINDOWS (Developer PowerShell for VS 2022)

```powershell
# Step 1: Open "Developer PowerShell for VS 2022" from Start Menu
# This sets up the MSVC environment automatically

# Step 2: Navigate to project directory
cd C:\path\to\aetheris

# Step 3: Clone vcpkg (if not already present)
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
cd ..

# Step 4: Create build directory
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release

# Step 5: Build (single command after setup)
cmake --build build --config Release --parallel

# Step 6: Run the browser
.\build\Release\aetheris.exe
```

**Single Command Build (after initial setup):**
```powershell
cmake -B build -S . && cmake --build build --config Release --parallel
```

---

### B) LINUX (Ubuntu/Debian)

```bash
# Step 1: Install system dependencies
sudo apt update
sudo apt install -y build-essential cmake git pkg-config libx11-dev

# Step 2: Clone vcpkg
cd /path/to/aetheris
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh
cd ..

# Step 3: Configure and build (single command)
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release && \
cmake --build build --parallel $(nproc)

# Step 4: Run the browser
./build/aetheris
```

**Single Command Build (after initial setup):**
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel $(nproc)
```

---

### C) MACOS (Homebrew / Clang)

```bash
# Step 1: Install dependencies via Homebrew
brew install cmake git

# Step 2: Clone vcpkg
cd /path/to/aetheris
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh
cd ..

# Step 3: Configure and build (single command)
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" && \
cmake --build build --parallel $(sysctl -n hw.ncpu)

# Step 4: Run the browser
./build/aetheris
```

**Single Command Build (after initial setup):**
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel $(sysctl -n hw.ncpu)
```

---

## OPTIMIZATION FLAGS APPLIED

### Windows (MSVC)
- `/O2` - Maximize speed
- `/Oi` - Generate intrinsic functions
- `/Ot` - Favor speed over size
- `/GL` - Whole program optimization
- `/Gy` - Function-level linking (dead code elimination)
- `/Qpar` - Auto-parallelization
- `/LTCG` - Link-time code generation
- `/OPT:REF` - Eliminate unreferenced functions/data
- `/OPT:ICF` - Identical COMDAT folding

### Linux/macOS (GCC/Clang)
- `-O3` - Maximum optimization
- `-march=native` - CPU-specific optimizations
- `-flto=auto` - Link-time optimization
- `-ffunction-sections` - Function-level linking
- `-fdata-sections` - Data section splitting
- `-fvisibility=hidden` - Hidden symbol visibility
- `-Wl,--gc-sections` - Dead code elimination
- `-Wl,--strip-all` - Strip all symbols

---

## EXPECTED OUTPUT SIZE

| Platform | Debug Size | Release Size | Stripped Release |
|----------|------------|--------------|------------------|
| Windows  | ~15 MB     | ~5 MB        | ~3.5 MB          |
| Linux    | ~12 MB     | ~4 MB        | ~2.8 MB          |
| macOS    | ~14 MB     | ~4.5 MB      | ~3 MB            |

*Note: Sizes may vary based on compiler version and optimization settings.*

---

## TROUBLESHOOTING

### Common Issues

**1. vcpkg not found**
```bash
# Ensure vcpkg is cloned in project root
git clone https://github.com/microsoft/vcpkg.git
```

**2. LZ4 library not found**
```bash
# vcpkg will auto-install via manifest mode
# Ensure you're using the vcpkg toolchain file
-DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
```

**3. X11 not found (Linux)**
```bash
sudo apt install libx11-dev
```

**4. Build fails with C++20 errors**
```bash
# Ensure CMake 3.20+ and modern compiler
cmake --version  # Should be >= 3.20
g++ --version    # Should be >= 10 or clang >= 10
```

**5. Linker errors on Windows**
```powershell
# Ensure Developer PowerShell is used (sets MSVC env)
# Or run vcvarsall.bat manually
& "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
```

---

## CLEAN BUILD

To perform a completely clean build:

```bash
# All platforms
rm -rf build/
rm -rf vcpkg_installed/

# Then rebuild
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

---

## PROJECT STRUCTURE

```
aetheris/
├── CMakeLists.txt           # Cross-platform build configuration
├── vcpkg.json               # Dependency manifest
├── include/
│   └── aetheris/
│       ├── core.hpp         # Core types and platform detection
│       ├── network/
│       │   └── socket_abstraction.hpp  # Cross-platform sockets
│       ├── cache/
│       │   └── ghost_cache.hpp         # LZ4 memory pool
│       └── ui/
│           └── window_manager.hpp      # Native window handling
└── src/
    ├── main.cpp             # Entry point
    ├── network/
    │   └── socket_abstraction.cpp
    ├── cache/
    │   └── ghost_cache.cpp
    └── ui/
        └── window_manager.cpp
```

---

## NEXT STEPS

After successful compilation:

1. **Test basic functionality**: Run `./build/aetheris` and verify window opens
2. **Check debug overlay**: FPS counter should display in top-left corner
3. **Test fullscreen**: Press F11 to toggle fullscreen mode
4. **Verify clean exit**: Press ESC or close window - check for zero memory leaks

For production builds, consider:
- Enabling additional security hardening flags
- Adding code signing (Windows/macOS)
- Implementing automatic updates
- Adding crash reporting

---

**Aetheris Browser** - Built for speed, privacy, and minimal footprint.

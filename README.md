# Aetheris Browser

## Hyper-optimized, Zero-bloat Cross-Platform Browser

**Aetheris** is an ultra-lightweight, privacy-focused browser built from the ground up with pure C++20. It features a minimal footprint (<5MB), aggressive optimizations, and native hardware-accelerated rendering across Windows, Linux, and macOS.

### Key Features

- **Zero Bloat**: No Electron, no Chromium, no massive frameworks
- **Pure C++20**: Modern, safe, and highly portable codebase
- **Cross-Platform**: Native builds for Windows (MSVC), Linux (GCC/Clang), macOS (Xcode/Clang)
- **LZ4 Ghost Cache**: In-memory compressed session storage with secure zeroing
- **Smart Script Bypass**: Detects HTML/CSS-only pages and skips JavaScript engine entirely
- **Native UI**: Hardware-accelerated window management via Win32/Cocoa/X11
- **vcpkg Integration**: Simple dependency management without dependency hell

### Build Requirements

| Platform | Compiler | CMake | Dependencies |
|----------|----------|-------|--------------|
| Windows | MSVC (VS 2022) | 3.20+ | vcpkg, Git |
| Linux | GCC 10+ / Clang 10+ | 3.20+ | libx11-dev, vcpkg |
| macOS | Xcode Clang | 3.20+ | vcpkg |

### Quick Start

```bash
# Clone vcpkg
git clone https://github.com/microsoft/vcpkg.git

# Single-command build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel

# Run
./build/aetheris
```

See [BUILD_GUIDE.md](BUILD_GUIDE.md) for detailed platform-specific instructions.

### Architecture

```
aetheris/
├── include/aetheris/
│   ├── core.hpp              # Platform detection, secure memory utilities
│   ├── network/
│   │   └── socket_abstraction.hpp  # Cross-platform TCP/HTTP sniffer
│   ├── cache/
│   │   └── ghost_cache.hpp         # LZ4 memory pool manager
│   └── ui/
│       └── window_manager.hpp      # Native window handling
└── src/
    └── main.cpp                    # Entry point & event loop
```

### Performance Targets

| Metric | Target |
|--------|--------|
| Binary Size | <5 MB (stripped release) |
| RAM Usage | <50 MB idle |
| Startup Time | <200ms |
| Cache Compression | 2-4x ratio |

### License

MIT License - See [LICENSE](LICENSE) for details.

---

**Built for speed. Designed for privacy.**

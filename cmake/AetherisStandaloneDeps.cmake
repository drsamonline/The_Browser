# Dependency discovery for the Aetheris standalone build profile.
#
# The standalone profile builds only src/ (AetherisRendering, AetherisRuntime,
# the `aetheris` executable) and Tests/AetherisRendering. It must configure on
# bare runners that have no vcpkg toolchain, so every dependency below is
# OPTIONAL: when a dependency is missing we compile a small shim instead of
# failing configuration. This keeps CI fast and green without sacrificing the
# real implementations when system libraries are present.

# ---------------------------------------------------------------------------
# LZ4 (used by include/aetheris/cache/ghost_cache.hpp)
# Falls back to a bundled byte-RLE shim exposing the three LZ4 entry points
# the cache actually uses (LZ4_compressBound, LZ4_compress_default,
# LZ4_decompress_safe). The shim lives in a private include directory that is
# only added when real LZ4 is unavailable, so production builds always use
# upstream lz4.h.
# ---------------------------------------------------------------------------
find_package(lz4 CONFIG QUIET)
if (TARGET lz4::lz4)
    set(AETHERIS_LZ4_TARGET lz4::lz4)
elseif (TARGET LZ4::lz4_static OR TARGET LZ4::lz4_shared)
    if (TARGET LZ4::lz4_static)
        set(AETHERIS_LZ4_TARGET LZ4::lz4_static)
    else()
        set(AETHERIS_LZ4_TARGET LZ4::lz4_shared)
    endif()
else()
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(AETHERIS_LZ4_PC IMPORTED_TARGET liblz4)
    endif()
    if (AETHERIS_LZ4_PC_FOUND)
        set(AETHERIS_LZ4_TARGET PkgConfig::AETHERIS_LZ4_PC)
    else()
        find_path(AETHERIS_LZ4_INCLUDE_DIR NAMES lz4.h)
        find_library(AETHERIS_LZ4_LIBRARY NAMES lz4 liblz4)
        if (AETHERIS_LZ4_INCLUDE_DIR AND AETHERIS_LZ4_LIBRARY)
            add_library(AetherisLZ4System INTERFACE)
            target_include_directories(AetherisLZ4System INTERFACE ${AETHERIS_LZ4_INCLUDE_DIR})
            target_link_libraries(AetherisLZ4System INTERFACE ${AETHERIS_LZ4_LIBRARY})
            set(AETHERIS_LZ4_TARGET AetherisLZ4System)
        endif()
    endif()
endif()

if (NOT AETHERIS_LZ4_TARGET)
    message(STATUS "Aetheris standalone: system LZ4 not found - using bundled compression shim")
    add_library(AetherisLZ4Shim INTERFACE)
    target_include_directories(AetherisLZ4Shim INTERFACE "${CMAKE_CURRENT_LIST_DIR}/fallback/include")
    target_sources(AetherisLZ4Shim INTERFACE "${CMAKE_CURRENT_LIST_DIR}/fallback/lz4_shim.cpp")
    set(AETHERIS_LZ4_TARGET AetherisLZ4Shim)
    set(AETHERIS_LZ4_IS_SHIM ON)
else()
    message(STATUS "Aetheris standalone: using LZ4 target ${AETHERIS_LZ4_TARGET}")
    set(AETHERIS_LZ4_IS_SHIM OFF)
endif()

# ---------------------------------------------------------------------------
# X11 (used by include/aetheris/ui/window_manager.hpp on Linux)
# When libX11 / its headers are unavailable (e.g. minimal containers), we do
# NOT hard-fail configuration. Instead the window manager compiles against a
# headless fallback (see window_manager.hpp, AETHERIS_HEADLESS_UI) so the
# `aetheris` executable still builds and runs without a display server.
# ---------------------------------------------------------------------------
set(AETHERIS_X11_TARGET "")
if (UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(AETHERIS_X11_PC IMPORTED_TARGET x11)
    endif()
    if (AETHERIS_X11_PC_FOUND)
        set(AETHERIS_X11_TARGET PkgConfig::AETHERIS_X11_PC)
    else()
        find_package(X11 MODULE QUIET)
        if (X11_FOUND)
            add_library(AetherisX11Imported INTERFACE)
            target_include_directories(AetherisX11Imported INTERFACE ${X11_INCLUDE_DIR})
            target_link_libraries(AetherisX11Imported INTERFACE ${X11_LIBRARIES})
            set(AETHERIS_X11_TARGET AetherisX11Imported)
        endif()
    endif()

    if (AETHERIS_X11_TARGET)
        message(STATUS "Aetheris standalone: using X11 target ${AETHERIS_X11_TARGET}")
    else()
        message(STATUS "Aetheris standalone: X11 not found - building the window manager in headless mode")
    endif()
endif()

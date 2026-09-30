/*
 * AETHERIS LZ4 FALLBACK SHIM - single translation unit.
 * Compiled only by the standalone CMake profile when no system/vcpkg LZ4
 * is available (see cmake/AetherisStandaloneDeps.cmake).
 */

#define AETHERIS_LZ4_FALLBACK_IMPLEMENTATION
#include "lz4.h"

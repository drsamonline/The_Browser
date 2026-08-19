#pragma once

/**
 * AETHERIS CORE HEADER
 * Cross-platform type definitions and platform detection
 */

#ifndef AETHERIS_CORE_HPP
#define AETHERIS_CORE_HPP

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <memory>
#include <array>
#include <span>
#include <concepts>
#include <type_traits>

// Platform Detection
#if defined(_WIN32) || defined(_WIN64)
    #define AETHERIS_PLATFORM_WINDOWS 1
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
#elif defined(__APPLE__) && defined(__MACH__)
    #define AETHERIS_PLATFORM_MACOS 1
#elif defined(__linux__)
    #define AETHERIS_PLATFORM_LINUX 1
#else
    #error "Unsupported platform"
#endif

// Compiler Detection
#if defined(_MSC_VER)
    #define AETHERIS_COMPILER_MSVC 1
    #define AETHERIS_FORCE_INLINE __forceinline
#elif defined(__clang__)
    #define AETHERIS_COMPILER_CLANG 1
    #define AETHERIS_FORCE_INLINE __attribute__((always_inline)) inline
#elif defined(__GNUC__)
    #define AETHERIS_COMPILER_GCC 1
    #define AETHERIS_FORCE_INLINE __attribute__((always_inline)) inline
#else
    #define AETHERIS_FORCE_INLINE inline
#endif

// Secure memory zeroing (prevents compiler optimization)
namespace aetheris {

AETHERIS_FORCE_INLINE void secure_zero_memory(void* ptr, size_t size) noexcept {
#if AETHERIS_PLATFORM_WINDOWS
    volatile unsigned char* p = static_cast<volatile unsigned char*>(ptr);
    while (size--) {
        *p++ = 0;
    }
#else
    // Use explicit_bzero on Unix-like systems
    #if defined(__OpenBSD__) || (defined(__GLIBC__) && __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 25)
        explicit_bzero(ptr, size);
    #elif defined(__APPLE__)
        memset_s(ptr, size, 0, size);
    #else
        // Fallback: volatile write
        volatile unsigned char* p = static_cast<volatile unsigned char*>(ptr);
        while (size--) {
            *p++ = 0;
        }
    #endif
#endif
}

// Compile-time string hash for fast lookups
constexpr uint32_t hash_string(const char* str, size_t len) noexcept {
    uint32_t hash = 5381;
    for (size_t i = 0; i < len; ++i) {
        hash = ((hash << 5) + hash) + static_cast<uint32_t>(str[i]);
    }
    return hash;
}

constexpr uint32_t hash_string(const char* str) noexcept {
    return hash_string(str, std::char_traits<char>::length(str));
}

} // namespace aetheris

#endif // AETHERIS_CORE_HPP

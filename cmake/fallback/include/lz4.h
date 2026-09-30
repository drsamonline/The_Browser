/*
 * AETHERIS LZ4 FALLBACK SHIM
 *
 * Minimal stand-in for <lz4.h>, used ONLY by the standalone CMake profile
 * when no system/vcpkg LZ4 is available. It exposes the exact three entry
 * points that aetheris/cache/ghost_cache.hpp calls, with identical error
 * semantics (0 on failure, positive byte count on success), implemented
 * with a simple run-length codec.
 *
 * This file is never compiled when a real LZ4 is found; ghost_cache.hpp
 * picks it up purely through the include path managed by
 * cmake/AetherisStandaloneDeps.cmake.
 */

#ifndef AETHERIS_LZ4_FALLBACK_H
#define AETHERIS_LZ4_FALLBACK_H

#define LZ4_VERSION_MAJOR 1
#define LZ4_VERSION_MINOR 0 /* shim, not upstream versioning */
#define LZ4_MAX_QUEUED_FRAMES 0

#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* Upper bound of the compressed size for a given input size. */
int LZ4_compressBound(int isize);

/* Compress `srcSize` bytes from `src` into `dst` (capacity `maxDstSize`).
 * Returns compressed size, or 0 on failure. */
int LZ4_compress_default(const char* src, char* dst, int srcSize, int maxDstSize);

/* Decompress `cSize` bytes from `src` into `dst` (capacity `maxDstSize`).
 * Returns decompressed size, or a negative value on failure. */
int LZ4_decompress_safe(const char* src, char* dst, int cSize, int maxDstSize);

#if defined(__cplusplus)
}
#endif

#endif /* AETHERIS_LZ4_FALLBACK_H */

#ifdef AETHERIS_LZ4_FALLBACK_IMPLEMENTATION

int LZ4_compressBound(int isize)
{
    if (isize < 0) {
        return 0;
    }
    /* Worst case for our RLE codec: one control byte per literal run of 255
     * plus the payload itself. Add generous slack to mirror LZ4's bound. */
    return isize + (isize / 255) + 16;
}

int LZ4_compress_default(const char* src, char* dst, int srcSize, int maxDstSize)
{
    if (!src || !dst || srcSize < 0 || maxDstSize <= 0) {
        return 0;
    }

    unsigned char* out = (unsigned char*)dst;
    int written = 0;
    int i = 0;

    while (i < srcSize) {
        /* Count a run of identical bytes (min useful run length 4). */
        int run = 1;
        while (i + run < srcSize && run < 255 + 3 && src[i + run] == src[i]) {
            ++run;
        }

        if (run >= 4) {
            int n = run - 3; /* 1..255 */
            if (written + 2 > maxDstSize) {
                return 0;
            }
            out[written++] = 0x80; /* tag byte: run follows */
            out[written++] = (unsigned char)n;
            out[written++] = (unsigned char)src[i];
            if (written + 1 > maxDstSize) {
                return 0;
            }
            /* store the repeated byte after the header pair */
            /* (header is tag+n, payload is the byte) */
            i += run;
            continue;
        }

        /* Literal run: gather bytes until the next repeatable run starts. */
        int lit_start = i;
        int lit_count = 0;
        while (i < srcSize) {
            int look = 1;
            while (i + look < srcSize && look < 4 && src[i + look] == src[i]) {
                ++look;
            }
            if (look >= 4) {
                break; /* let the outer loop emit a run */
            }
            ++i;
            ++lit_count;
            if (lit_count == 255) {
                break;
            }
        }

        if (lit_count == 0) {
            lit_count = 1; /* defensive: always make progress */
            i = lit_start + 1;
        }
        if (written + 1 + lit_count > maxDstSize) {
            return 0;
        }
        out[written++] = (unsigned char)lit_count; /* 1..255, high bit clear */
        for (int k = 0; k < lit_count; ++k) {
            out[written++] = (unsigned char)src[lit_start + k];
        }
    }

    return written;
}

int LZ4_decompress_safe(const char* src, char* dst, int cSize, int maxDstSize)
{
    if (!src || !dst || cSize < 0 || maxDstSize < 0) {
        return -1;
    }

    const unsigned char* in = (const unsigned char*)src;
    unsigned char* out = (unsigned char*)dst;
    int ip = 0;
    int op = 0;

    while (ip < cSize) {
        unsigned char token = in[ip++];
        if (token & 0x80) {
            /* run: [0x80][n][byte] -> (n+3) copies */
            if (ip + 2 > cSize) {
                return -1;
            }
            int n = in[ip++] + 3;
            unsigned char value = in[ip++];
            if (op + n > maxDstSize) {
                return -1;
            }
            for (int k = 0; k < n; ++k) {
                out[op++] = value;
            }
        } else {
            int n = token; /* literal count, 1..255 */
            if (n == 0 || ip + n > cSize || op + n > maxDstSize) {
                return -1;
            }
            for (int k = 0; k < n; ++k) {
                out[op++] = in[ip++];
            }
        }
    }

    return op;
}

#endif /* AETHERIS_LZ4_FALLBACK_IMPLEMENTATION */

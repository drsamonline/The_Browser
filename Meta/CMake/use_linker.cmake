# Copyright (c) 2022, Andrew Kaster <akaster@serenityos.org>
# Copyright (c) 2023, Daniel Bertalan <dani@danielbertalan.dev>
#
# SPDX-License-Identifier: BSD-2-Clause
#

if (NOT APPLE AND NOT ANDROID AND NOT VCPKG_TARGET_ANDROID AND NOT WIN32 AND NOT LAGOM_USE_LINKER)
    find_program(LLD_LINKER NAMES "ld.lld")
    if (LLD_LINKER)
        message(STATUS "Using LLD to link Lagom.")
        set(LAGOM_USE_LINKER "lld" CACHE STRING "" FORCE)
    else()
        find_program(MOLD_LINKER NAMES "ld.mold")
        if (MOLD_LINKER)
            message(STATUS "Using mold to link Lagom.")
            set(LAGOM_USE_LINKER "mold" CACHE STRING "" FORCE)
        endif()
    endif()
endif()

if (WIN32 AND NOT LAGOM_USE_LINKER)
    string(TOUPPER "${AETHERIS_WINDOWS_LINKER_MODE}" aetheris_windows_linker_mode)

    if (NOT aetheris_windows_linker_mode MATCHES "^(AUTO|SYSTEM|LLD)$")
        message(FATAL_ERROR
            "Invalid AETHERIS_WINDOWS_LINKER_MODE='${AETHERIS_WINDOWS_LINKER_MODE}'. "
            "Expected AUTO, SYSTEM, or LLD.")
    endif()

    if (aetheris_windows_linker_mode STREQUAL "LLD")
        find_program(AETHERIS_LLD_LINK NAMES "lld-link")
        if (NOT AETHERIS_LLD_LINK)
            message(FATAL_ERROR
                "AETHERIS_WINDOWS_LINKER_MODE=LLD requires lld-link.exe, but it was not found. "
                "Install the LLVM linker or configure with -DAETHERIS_WINDOWS_LINKER_MODE=SYSTEM.")
        endif()
        message(STATUS "Using lld-link for Windows linking.")
        set(LAGOM_USE_LINKER "lld" CACHE STRING "" FORCE)
    elseif (aetheris_windows_linker_mode STREQUAL "AUTO")
        find_program(AETHERIS_LLD_LINK NAMES "lld-link")
        if (AETHERIS_LLD_LINK)
            message(STATUS "Using lld-link for Windows linking.")
            set(LAGOM_USE_LINKER "lld" CACHE STRING "" FORCE)
        else()
            message(STATUS "lld-link was not found; using the linker selected by the active Windows toolchain.")
        endif()
    else()
        message(STATUS "Using the linker selected by the active Windows toolchain.")
    endif()
endif()

if (LAGOM_USE_LINKER)
    # FIXME: Move to only setting CMAKE_LINKER_TYPE once we drop support for CMake < 3.29
    # NOTE: CMAKE_LINKER_TYPE doesn't work on BSDs on CMake < 4.2
    if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.29 AND NOT BSD)
        string(TOUPPER ${LAGOM_USE_LINKER} linker_type)
        set(CMAKE_LINKER_TYPE ${linker_type})
    else()
        set(LINKER_FLAG "-fuse-ld=${LAGOM_USE_LINKER}")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${LINKER_FLAG}")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${LINKER_FLAG}")
        set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${LINKER_FLAG}")
    endif()
endif()

if (LAGOM_LINK_POOL_SIZE)
    set_property(GLOBAL PROPERTY JOB_POOLS link_pool=${LAGOM_LINK_POOL_SIZE})
    set(CMAKE_JOB_POOL_LINK link_pool CACHE STRING "Linking job pool")
endif()

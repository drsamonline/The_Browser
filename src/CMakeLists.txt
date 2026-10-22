add_library(AetherisRendering STATIC
    rendering/clip.cpp
    rendering/geometry.cpp
    rendering/color.cpp
    rendering/image_output.cpp
    rendering/resource.cpp
    rendering/content_type.cpp
    rendering/resource_loader.cpp
    rendering/security_policy.cpp
    rendering/secure_resource_loader.cpp
    rendering/cookie_jar.cpp
    rendering/http_resource_backend.cpp
    rendering/document_runtime.cpp
    rendering/file_resource_backend.cpp
    rendering/url.cpp
    rendering/navigation.cpp
    rendering/page_lifecycle.cpp
    rendering/form_runtime.cpp
    rendering/visual_state.cpp
    rendering/viewport.cpp
    rendering/document_interaction.cpp
    rendering/browser_session.cpp
    rendering/browser_application.cpp
    rendering/browser_chrome.cpp
    rendering/image.cpp
    rendering/font.cpp
    rendering/bitmap_font.cpp
    rendering/png.cpp
    rendering/jpeg.cpp
    rendering/gif.cpp
    rendering/css_parser.cpp
    rendering/css_tokenizer.cpp
    rendering/dom.cpp
    rendering/document.cpp
    rendering/html_tokenizer.cpp
    rendering/html_tree_builder.cpp
    rendering/layout.cpp
    rendering/paint_executor.cpp
    rendering/render_document.cpp
    rendering/render_tree.cpp
    rendering/software_surface.cpp
    rendering/style.cpp
    rendering/text_layout.cpp
)

# Consumers (including the Tests/AetherisRendering suite) include these headers
# as <name.hpp>, so the directory must be an INTERFACE include dir as well.
target_include_directories(AetherisRendering
    PUBLIC
        # ${CMAKE_CURRENT_SOURCE_DIR} lets consumers include headers as
        # "rendering/foo.hpp"; ${CMAKE_CURRENT_SOURCE_DIR}/rendering keeps the
        # legacy bare "foo.hpp" style working.
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/rendering>
)

target_compile_features(AetherisRendering PUBLIC cxx_std_20)

target_compile_definitions(AetherisRendering
    PUBLIC
        AETHERIS_RENDERING_FOUNDATION=1
)

# ---------------------------------------------------------------------------
# Prototype runtime components (network, cache, UI window manager).
# These are header-driven implementations with thin .cpp stubs; they are wired
# into the build graph here so they stay compile-tested instead of dormant.
# ---------------------------------------------------------------------------
add_library(AetherisRuntime STATIC
    network/socket_abstraction.cpp
    network/tls.cpp
    cache/ghost_cache.cpp
    ui/window_manager.cpp
)

target_include_directories(AetherisRuntime
    PUBLIC
        $<BUILD_INTERFACE:${LADYBIRD_SOURCE_DIR}/include>
)

target_compile_features(AetherisRuntime PUBLIC cxx_std_20)

# Optional dependency discovery (LZ4 + X11) with graceful fallbacks: when a
# dependency is missing we compile the bundled shim / headless backend instead
# of failing configuration, so minimal CI runners can build the full tree.
include("${LADYBIRD_SOURCE_DIR}/cmake/AetherisStandaloneDeps.cmake")

target_link_libraries(AetherisRuntime PUBLIC ${AETHERIS_LZ4_TARGET})

if (AETHERIS_X11_TARGET)
    target_link_libraries(AetherisRuntime PUBLIC ${AETHERIS_X11_TARGET})
else()
    target_compile_definitions(AetherisRuntime PUBLIC AETHERIS_HEADLESS_UI=1)
endif()

# Optional TLS: when OpenSSL is available, the runtime gains real HTTPS with
# certificate and hostname verification (vetted crypto). Without it, the TLS
# stream compiles to a stub that reports TLS as unavailable.
find_package(OpenSSL QUIET)
if (OpenSSL_FOUND)
    target_compile_definitions(AetherisRuntime PRIVATE AETHERIS_HAS_TLS=1)
    target_link_libraries(AetherisRuntime PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    message(STATUS "Aetheris: OpenSSL found - HTTPS/TLS enabled")
else()
    target_compile_definitions(AetherisRuntime PRIVATE AETHERIS_HAS_TLS=0)
    message(STATUS "Aetheris: OpenSSL not found - HTTPS/TLS disabled")
endif()

# ---------------------------------------------------------------------------
# The Aetheris browser executable: main entry point over the prototype runtime.
# ---------------------------------------------------------------------------
add_executable(aetheris
    main.cpp
)

target_link_libraries(aetheris PRIVATE AetherisRuntime AetherisRendering)

set_target_properties(aetheris PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ---------------------------------------------------------------------------
# Minimum-footprint profile (AETHERIS_SIZE_OPTIMIZED): size-first codegen,
# dead-section elimination, LTO and a post-link strip. Scoped to the Aetheris
# targets so the upstream-derived tree is unaffected.
# ---------------------------------------------------------------------------
if (AETHERIS_SIZE_OPTIMIZED)
    set(AETHERIS_SIZE_TARGETS AetherisRendering AetherisRuntime aetheris)

    if (MSVC)
        set(AETHERIS_SIZE_COMPILE /O1 /Gy /Gw)
        set(AETHERIS_SIZE_LINK /OPT:REF /OPT:ICF)
    else()
        set(AETHERIS_SIZE_COMPILE -Os -ffunction-sections -fdata-sections
                                  -fno-unwind-tables -fno-asynchronous-unwind-tables)
        if (APPLE)
            set(AETHERIS_SIZE_LINK -Wl,-dead_strip)
        else()
            set(AETHERIS_SIZE_LINK -Wl,--gc-sections)
        endif()
    endif()

    foreach (target IN LISTS AETHERIS_SIZE_TARGETS)
        target_compile_options(${target} PRIVATE ${AETHERIS_SIZE_COMPILE})
        target_link_options(${target} PRIVATE ${AETHERIS_SIZE_LINK})
    endforeach()

    # Link-time optimization for this profile is provided by the project-wide
    # ENABLE_LTO_FOR_RELEASE option (see Meta/CMake/compile_options.cmake),
    # which is ON by default for Release builds.

    # Strip the shipped executable when the toolchain provides `strip`.
    if (NOT MSVC AND NOT APPLE)
        find_program(AETHERIS_STRIP_TOOL strip)
        if (AETHERIS_STRIP_TOOL)
            add_custom_command(TARGET aetheris POST_BUILD
                COMMAND "${AETHERIS_STRIP_TOOL}" --strip-unneeded "$<TARGET_FILE:aetheris>"
                COMMENT "Stripping aetheris for minimum size"
            )
        endif()
    endif()
endif()

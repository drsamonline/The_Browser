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
    rendering/font_library.cpp
    rendering/glyph_rasterizer.cpp
    rendering/ttf_font.cpp
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
    rendering/svg.cpp
    rendering/webp.cpp
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
target_link_libraries(AetherisRendering PUBLIC AetherisGumbo)

# Optional WebP: link the system libwebp (lossy VP8 + lossless VP8L) when it is
# available. The two decoder functions are declared directly, so only the
# library itself is required.
if (NOT AETHERIS_WEBP_LIBRARY)
    find_library(AETHERIS_WEBP_LIBRARY NAMES webp)
endif()
if (AETHERIS_WEBP_LIBRARY)
    target_compile_definitions(AetherisRendering PRIVATE AETHERIS_HAS_WEBP=1)
    target_link_libraries(AetherisRendering PRIVATE ${AETHERIS_WEBP_LIBRARY})
    message(STATUS "Aetheris: libwebp found - WebP enabled")
else()
    target_compile_definitions(AetherisRendering PRIVATE AETHERIS_HAS_WEBP=0)
    message(STATUS "Aetheris: libwebp not found - WebP disabled")
endif()

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
# The Aetheris mini JavaScript engine: a self-contained interpreter for the
# runtime (the full LibJS engine needs Rust + a clang GC plugin + ICU and is not
# buildable in the standalone profile).
# ---------------------------------------------------------------------------
add_library(AetherisJS STATIC
    js/js.cpp
)

# The engine itself is the vendored QuickJS.
target_link_libraries(AetherisJS PUBLIC AetherisQuickJS)

target_include_directories(AetherisJS
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/js>
)

target_compile_features(AetherisJS PUBLIC cxx_std_20)

# ---------------------------------------------------------------------------
# QuickJS: the JavaScript engine for the standalone profile (vendored, MIT).
# Builds with a C compiler only, which is why it can replace the upstream LibJS
# engine (that one needs Rust, a clang GC plugin and ICU).
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# gumbo-parser: the HTML5 parsing algorithm (vendored, Apache-2.0).
# ---------------------------------------------------------------------------
add_library(AetherisGumbo STATIC
    ${CMAKE_SOURCE_DIR}/third_party/gumbo/attribute.c
    ${CMAKE_SOURCE_DIR}/third_party/gumbo/char_ref.c
    ${CMAKE_SOURCE_DIR}/third_party/gumbo/error.c
    ${CMAKE_SOURCE_DIR}/third_party/gumbo/parser.c
    ${CMAKE_SOURCE_DIR}/third_party/gumbo/string_buffer.c
    ${CMAKE_SOURCE_DIR}/third_party/gumbo/string_piece.c
    ${CMAKE_SOURCE_DIR}/third_party/gumbo/tag.c
    ${CMAKE_SOURCE_DIR}/third_party/gumbo/tokenizer.c
    ${CMAKE_SOURCE_DIR}/third_party/gumbo/utf8.c
    ${CMAKE_SOURCE_DIR}/third_party/gumbo/util.c
    ${CMAKE_SOURCE_DIR}/third_party/gumbo/vector.c
)

target_include_directories(AetherisGumbo
    SYSTEM PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/gumbo>
)
set_target_properties(AetherisGumbo PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)

# Vendored third-party code is not held to this project's warning set (-Werror).
target_compile_options(AetherisGumbo PRIVATE
    $<$<C_COMPILER_ID:MSVC>:/W0>
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>
)

add_library(AetherisQuickJS STATIC
    ${CMAKE_SOURCE_DIR}/third_party/quickjs/quickjs.c
    ${CMAKE_SOURCE_DIR}/third_party/quickjs/libregexp.c
    ${CMAKE_SOURCE_DIR}/third_party/quickjs/libunicode.c
    ${CMAKE_SOURCE_DIR}/third_party/quickjs/dtoa.c
)

target_include_directories(AetherisQuickJS
    SYSTEM PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/quickjs>
)

set_target_properties(AetherisQuickJS PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# Vendored third-party code is not held to this project's warning set (-Werror).
target_compile_options(AetherisQuickJS PRIVATE
    $<$<C_COMPILER_ID:MSVC>:/W0>
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>
)

target_link_libraries(AetherisQuickJS PUBLIC m)

# ---------------------------------------------------------------------------
# The Aetheris browser executable: main entry point over the prototype runtime.
# ---------------------------------------------------------------------------
add_executable(aetheris
    main.cpp
)

target_link_libraries(aetheris PRIVATE AetherisRuntime AetherisRendering AetherisJS)

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

# Size reporting: never fails the build. The browser is expected to grow as
# features land, so this surfaces the number (and the drift from the recorded
# reference) instead of gating on it.
if(AETHERIS_SIZE_OPTIMIZED)
    add_custom_command(TARGET aetheris POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DBINARY=$<TARGET_FILE:aetheris>
            -Dreference=2490416
            -P ${CMAKE_SOURCE_DIR}/Tools/report_binary_size.cmake
        COMMENT "Reporting the binary size"
    )
    if(AETHERIS_ENABLE_TESTING AND BUILD_TESTING)
        add_test(NAME AetherisSizeReport
            COMMAND ${CMAKE_COMMAND}
                -DBINARY=$<TARGET_FILE:aetheris>
                -Dreference=2490416
                -P ${CMAKE_SOURCE_DIR}/Tools/report_binary_size.cmake
        )
    endif()
endif()

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
    cache/ghost_cache.cpp
    ui/window_manager.cpp
)

target_include_directories(AetherisRuntime
    PUBLIC
        $<BUILD_INTERFACE:${LADYBIRD_SOURCE_DIR}/include>
)

target_compile_features(AetherisRuntime PUBLIC cxx_std_20)

find_package(PkgConfig QUIET)
if (NOT WIN32 AND NOT APPLE)
    if (PkgConfig_FOUND)
        pkg_check_modules(AETHERIS_X11 REQUIRED IMPORTED_TARGET x11)
        target_link_libraries(AetherisRuntime PUBLIC PkgConfig::AETHERIS_X11)
    else()
        find_package(X11 REQUIRED)
        target_link_libraries(AetherisRuntime PUBLIC ${X11_LIBRARIES})
        target_include_directories(AetherisRuntime PUBLIC ${X11_INCLUDE_DIR})
    endif()
endif()

find_package(LZ4 QUIET)
if (TARGET LZ4::lz4_static OR TARGET LZ4::lz4_shared)
    if (TARGET LZ4::lz4_static)
        target_link_libraries(AetherisRuntime PUBLIC LZ4::lz4_static)
    else()
        target_link_libraries(AetherisRuntime PUBLIC LZ4::lz4_shared)
    endif()
else()
    # Fall back to the system library discovered via pkg-config / raw search.
    pkg_check_modules(AETHERIS_LZ4 IMPORTED_TARGET liblz4)
    if (AETHERIS_LZ4_FOUND)
        target_link_libraries(AetherisRuntime PUBLIC PkgConfig::AETHERIS_LZ4)
    else()
        find_path(AETHERIS_LZ4_INCLUDE_DIR NAMES lz4.h REQUIRED)
        find_library(AETHERIS_LZ4_LIBRARY NAMES lz4 liblz4 REQUIRED)
        target_include_directories(AetherisRuntime PUBLIC ${AETHERIS_LZ4_INCLUDE_DIR})
        target_link_libraries(AetherisRuntime PUBLIC ${AETHERIS_LZ4_LIBRARY})
    endif()
endif()

# ---------------------------------------------------------------------------
# The Aetheris browser executable: main entry point over the prototype runtime.
# ---------------------------------------------------------------------------
add_executable(aetheris
    main.cpp
)

target_link_libraries(aetheris PRIVATE AetherisRuntime)

set_target_properties(aetheris PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

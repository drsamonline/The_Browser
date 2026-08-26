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

target_include_directories(AetherisRendering
    PUBLIC
        ${LADYBIRD_SOURCE_DIR}/src/rendering
)

target_compile_features(AetherisRendering PUBLIC cxx_std_20)

target_compile_definitions(AetherisRendering
    PUBLIC
        AETHERIS_RENDERING_FOUNDATION=1
)

add_library(AetherisRendering STATIC
    rendering/css_parser.cpp
    rendering/css_tokenizer.cpp
    rendering/dom.cpp
    rendering/document.cpp
    rendering/html_tokenizer.cpp
    rendering/html_tree_builder.cpp
    rendering/style.cpp
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

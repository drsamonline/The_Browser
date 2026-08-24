add_library(AetherisRendering STATIC
    rendering/html_tokenizer.cpp
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

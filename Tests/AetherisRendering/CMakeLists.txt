add_executable(TestAetherisHtmlTokenizer
    TestHtmlTokenizer.cpp
)

target_link_libraries(TestAetherisHtmlTokenizer PRIVATE AetherisRendering)

add_test(
    NAME AetherisHtmlTokenizer
    COMMAND TestAetherisHtmlTokenizer
)

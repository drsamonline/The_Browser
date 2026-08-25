add_executable(TestAetherisHtmlTokenizer
    TestHtmlTokenizer.cpp
)

target_link_libraries(TestAetherisHtmlTokenizer PRIVATE AetherisRendering)

add_test(
    NAME AetherisHtmlTokenizer
    COMMAND TestAetherisHtmlTokenizer
)

add_executable(TestAetherisDomBuilder
    TestDomBuilder.cpp
)

target_link_libraries(TestAetherisDomBuilder PRIVATE AetherisRendering)

add_test(
    NAME AetherisDomBuilder
    COMMAND TestAetherisDomBuilder
)

add_executable(TestAetherisCss
    TestCss.cpp
)

target_link_libraries(TestAetherisCss PRIVATE AetherisRendering)

add_test(
    NAME AetherisCss
    COMMAND TestAetherisCss
)

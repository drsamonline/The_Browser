add_executable(TestAetherisHtmlTokenizer
    TestHtmlTokenizer.cpp
)
target_link_libraries(TestAetherisHtmlTokenizer PRIVATE AetherisRendering)
add_test(NAME AetherisHtmlTokenizer COMMAND TestAetherisHtmlTokenizer)

add_executable(TestAetherisDomBuilder
    TestDomBuilder.cpp
)
target_link_libraries(TestAetherisDomBuilder PRIVATE AetherisRendering)
add_test(NAME AetherisDomBuilder COMMAND TestAetherisDomBuilder)

add_executable(TestAetherisCss
    TestCss.cpp
)
target_link_libraries(TestAetherisCss PRIVATE AetherisRendering)
add_test(NAME AetherisCss COMMAND TestAetherisCss)

add_executable(TestAetherisCascade
    TestCascade.cpp
)
target_link_libraries(TestAetherisCascade PRIVATE AetherisRendering)
add_test(NAME AetherisCascade COMMAND TestAetherisCascade)

add_executable(TestAetherisLayout
    TestLayout.cpp
)
target_link_libraries(TestAetherisLayout PRIVATE AetherisRendering)
add_test(NAME AetherisLayout COMMAND TestAetherisLayout)

add_executable(TestAetherisBoxModel
    TestBoxModel.cpp
)
target_link_libraries(TestAetherisBoxModel PRIVATE AetherisRendering)
add_test(NAME AetherisBoxModel COMMAND TestAetherisBoxModel)


add_executable(TestAetherisTextLayout
    TestTextLayout.cpp
)
target_link_libraries(TestAetherisTextLayout PRIVATE AetherisRendering)
add_test(NAME AetherisTextLayout COMMAND TestAetherisTextLayout)


add_executable(TestAetherisPaintBackend
    TestPaintBackend.cpp
)
target_link_libraries(TestAetherisPaintBackend PRIVATE AetherisRendering)
add_test(NAME AetherisPaintBackend COMMAND TestAetherisPaintBackend)


add_executable(TestAetherisPaintEffects
    TestPaintEffects.cpp
)
target_link_libraries(TestAetherisPaintEffects PRIVATE AetherisRendering)
add_test(NAME AetherisPaintEffects COMMAND TestAetherisPaintEffects)

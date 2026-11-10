#include <color.hpp>
#include <css_parser.hpp>
#include <css_tokenizer.hpp>

#include <cstdlib>
#include <iostream>

using namespace aetheris::rendering;

static void require(bool condition, char const* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    CssTokenizer tokenizer("div > .card, * { margin: 10px 20px; } /* comment */");
    auto tokens = tokenizer.tokenize();
    bool child = false;
    bool universal = false;
    for (auto const& token : tokens) {
        child |= token.type == CssTokenType::ChildCombinator;
        universal |= token.type == CssTokenType::UniversalSelector;
    }
    require(child, "child combinator token");
    require(universal, "universal selector token");

    auto sheet = CssParser {}.parse("h1, .title { color: red; margin: 12px; }");
    require(sheet.rules.size() == 1, "rule count");
    require(sheet.rules[0].selectors.size() == 2, "selector list");
    require(sheet.rules[0].declarations.size() == 2, "declaration count");
    require(sheet.rules[0].source_order == 0, "source order");

    // Named colours come from the full CSS table (upstream LibGfx data).
    {
        auto purple = Color::parse("rebeccapurple");
        require(purple.has_value(), "rebeccapurple parses");
        require(purple->red == 0x66 && purple->green == 0x33 && purple->blue == 0x99, "rebeccapurple value");
        auto crimson = Color::parse("crimson");
        require(crimson.has_value() && crimson->red == 0xdc, "crimson parses");
        require(!Color::parse("notacolour").has_value(), "unknown colour rejected");
    }


    // Media queries are resolved against the screen viewport (1024x768).
    {
        CssParser parser;
        // `media_sheet`, not `sheet`: an outer sheet is in scope here and C4456 is
        // fatal under /WX.
        auto media_sheet = parser.parse(
            "p { color: black; }"
            "@media screen { p { color: green; } }"
            "@media print { p { color: red; } }"
            "@media (min-width: 600px) { #a { color: blue; } }"
            "@media (max-width: 500px) { #b { color: orange; } }");
        // screen and min-width survive; print and max-width are dropped.
        require(media_sheet.rules.size() == 3, "media queries filter by the viewport");
        bool has_blue = false;
        for (auto const& rule : media_sheet.rules)
            for (auto const& selector : rule.selectors)
                if (selector == "#a")
                    has_blue = true;
        require(has_blue, "min-width rule is kept");
        for (auto const& rule : media_sheet.rules)
            for (auto const& selector : rule.selectors) {
                require(selector != "#b", "max-width rule is dropped");
                require(selector.rfind("@media", 0) != 0, "media blocks do not leak as selectors");
                require(selector.find('}') == std::string::npos, "block braces do not leak into selectors");
            }
    }

    return 0;
}

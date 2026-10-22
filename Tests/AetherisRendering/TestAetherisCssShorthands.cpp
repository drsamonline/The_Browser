#include "css_parser.hpp"
#include "style.hpp"

#include <cstdio>
#include <cstdlib>

using namespace aetheris::rendering;

static void require(bool condition, char const* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

static bool equals(StyleProperties const& style, char const* property, char const* expected)
{
    auto value = style.get(property);
    return value && *value == expected;
}

int main()
{
    DomNode node(DomNodeType::Element, "div");
    auto sheet = CssParser {}.parse(
        "div { border: 2px dashed red; background: #102030 url('bg.png') no-repeat center; }"
        "p { border: 1px solid #0a0a0a; }");

    StyleResolver resolver;
    auto style = resolver.resolve(node, sheet);

    // `border` shorthand expands to width (all sides), style and colour.
    require(equals(style, "border-top-width", "2px"), "border width top");
    require(equals(style, "border-left-width", "2px"), "border width left");
    require(equals(style, "border-style", "dashed"), "border style");
    require(equals(style, "border-color", "red"), "border colour");

    // `background` shorthand extracts the colour and the url() image.
    require(equals(style, "background-color", "#102030"), "background colour");
    auto image = style.get("background-image");
    require(image && image->rfind("url(bg.png)", 0) == 0, "background image");

    // Colour-only border shorthand.
    DomNode paragraph(DomNodeType::Element, "p");
    auto paragraph_style = resolver.resolve(paragraph, sheet);
    require(equals(paragraph_style, "border-top-width", "1px"), "border shorthand width");
    require(equals(paragraph_style, "border-style", "solid"), "border shorthand style");
    require(equals(paragraph_style, "border-color", "#0a0a0a"), "border shorthand colour");

    return 0;
}

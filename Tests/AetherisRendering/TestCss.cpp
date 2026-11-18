#include <color.hpp>
#include <css_parser.hpp>
#include <document.hpp>
#include <style.hpp>
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
    // A generated box's style: `::before`/`::after` resolve their own declarations, matched by
    // stripping the pseudo-element suffix and matching the rest against the element. Upstream keeps a
    // pseudo-element's computed style beside the element's (DOM/Element.cpp) and generates a box only
    // when `content` is neither normal nor none (Rust/src/layout/tree_builder.rs).
    {
        CssParser pseudo_parser;
        auto pseudo_sheet = pseudo_parser.parse(
            "p { color: #00ff00; }"
            "p::before { content: 'A'; color: #ff00ff; }"
            "p::after { content: 'B'; }"
            "p.card::before { content: 'C'; font-weight: bold; }");
        auto document = Document::parse_html("<p class='card'>x</p>");
        auto* paragraph = document.root().children.empty() ? nullptr : document.root().children.front().get();
        while (paragraph && paragraph->name != "p")
            paragraph = nullptr;
        require(paragraph != nullptr, "the fixture has a paragraph");

        StyleResolver resolver;
        auto element_style = resolver.resolve(*paragraph, pseudo_sheet);
        require(element_style.get("color") && *element_style.get("color") == "#00ff00", "the element keeps its own colour");

        auto before = resolver.resolve_pseudo_element(*paragraph, pseudo_sheet, "before", &element_style);
        // The CSS parser stores a string value without its quotes, so `content: 'C'` reads back as C.
        require(before.get("content") && *before.get("content") == "C", "the more specific ::before wins");
        require(before.get("font-weight") && *before.get("font-weight") == "bold", "its other declarations apply");
        // Colour comes from the *less* specific rule, which also sets it: specificity decides conflicts,
        // not which rules apply.
        require(before.get("color") && *before.get("color") == "#ff00ff", "a declaration the other rule does not set still applies");

        auto after = resolver.resolve_pseudo_element(*paragraph, pseudo_sheet, "after", &element_style);
        require(after.get("content") && *after.get("content") == "B", "::after resolves separately");

        // ::after has no colour of its own, so it inherits the element's.
        require(after.get("color") && *after.get("color") == "#00ff00", "::after inherits from the element");
        require(!after.get("font-weight"), "::after does not pick up ::before's declarations");
    }

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

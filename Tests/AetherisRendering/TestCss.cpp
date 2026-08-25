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
    return 0;
}

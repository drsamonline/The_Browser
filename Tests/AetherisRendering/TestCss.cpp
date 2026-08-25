#include <css_parser.hpp>
#include <css_tokenizer.hpp>
#include <style.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace aetheris::rendering;

static void require(bool condition, std::string const& message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    {
        CssTokenizer tokenizer("h1, .title { color: red; margin: 12px; } /* ignored */");
        auto tokens = tokenizer.tokenize();
        require(tokens.front().type == CssTokenType::Identifier, "identifier token");
        require(tokens.back().type == CssTokenType::EndOfFile, "EOF token");

        bool found_comment_text = false;
        for (auto const& token : tokens) {
            if (token.value.find("ignored") != std::string::npos)
                found_comment_text = true;
        }
        require(!found_comment_text, "comments skipped");
    }

    CssParser parser;
    auto sheet = parser.parse("h1, .title { color: red; margin: 12px; } #hero { color: blue; }");
    require(sheet.rules.size() == 2, "rule count");
    require(sheet.rules[0].selectors.size() == 2, "selector list");
    require(sheet.rules[0].declarations.size() == 2, "declaration count");
    require(sheet.rules[0].declarations[0].property == "color", "property name");
    require(sheet.rules[0].declarations[1].value == "12px", "unit value");

    DomNode node(DomNodeType::Element, "h1");
    node.attributes["id"] = "hero";
    node.attributes["class"] = "title main";

    StyleResolver resolver;
    auto style = resolver.resolve(node, sheet);
    require(style.get("margin") && *style.get("margin") == "12px", "tag and class rules apply");
    require(style.get("color") && *style.get("color") == "blue", "later matching rule wins");

    return 0;
}

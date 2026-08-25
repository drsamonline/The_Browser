#include <css_parser.hpp>
#include <style.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>

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
    DomNode root(DomNodeType::Element, "div");
    root.attributes["id"] = "app";
    root.attributes["class"] = "container";

    auto section = std::make_unique<DomNode>(DomNodeType::Element, "section");
    section->attributes["class"] = "panel";
    auto paragraph = std::make_unique<DomNode>(DomNodeType::Element, "p");
    paragraph->attributes["class"] = "note featured";
    auto& paragraph_ref = section->append_child(std::move(paragraph));
    root.append_child(std::move(section));

    auto sheet = CssParser {}.parse(
        "* { color: black; } "
        "div { color: red; } "
        ".note { color: green; } "
        "p.note.featured { color: blue; } "
        "div .note { margin: 4px 8px; } "
        "section > p { padding: 2px; } "
        "#app .note { color: purple; }");

    StyleResolver resolver;
    auto root_style = resolver.resolve(root, sheet);
    auto paragraph_style = resolver.resolve(paragraph_ref, sheet, &root_style);

    require(paragraph_style.get("color") && *paragraph_style.get("color") == "purple", "ID specificity wins");
    require(paragraph_style.get("margin-left") && *paragraph_style.get("margin-left") == "8px", "margin shorthand expansion");
    require(paragraph_style.get("padding-top") && *paragraph_style.get("padding-top") == "2px", "child selector matches");
    return 0;
}

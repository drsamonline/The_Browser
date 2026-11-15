// justify-content: space-between pushes flex items to the edges.
//
// require() rather than assert(): assert() disappears under -DNDEBUG, so an
// assert-based test passes silently in Release and fails to compile under
// -Werror because the variable it used becomes unused.

#include "css_parser.hpp"
#include "dom.hpp"
#include "layout.hpp"

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

int main()
{
    auto root = std::make_unique<DomNode>();
    root->type = DomNodeType::Document;
    auto box = std::make_unique<DomNode>();
    box->type = DomNodeType::Element;
    box->name = "div";
    for (int i = 0; i < 2; ++i) {
        auto child = std::make_unique<DomNode>();
        child->type = DomNodeType::Element;
        child->name = "span";
        box->append_child(std::move(child));
    }
    root->append_child(std::move(box));

    CssParser parser;
    auto sheet = parser.parse("div{display:flex;width:100;justify-content:space-between;}span{width:20;height:10;}");
    auto tree = LayoutTreeBuilder {}.build(*root, sheet);
    LayoutEngine {}.layout(*tree, 100);

    auto& container = *tree->children[0];
    require(container.children[1]->rect.x > container.children[0]->rect.x + 20,
        "space-between separates the items");

    std::printf("advanced flex layout: space-between verified\n");
    return 0;
}

// Flex layout: gap, column direction and flex-grow.
//
// These use require() rather than assert(): assert() compiles away under
// -DNDEBUG, which is how Release builds are configured, so assert-based tests
// silently pass in Release and fail to compile under -Werror because the
// variables they referenced become unused.

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

static std::unique_ptr<LayoutNode> build(char const* css)
{
    auto root = std::make_unique<DomNode>();
    root->type = DomNodeType::Document;
    auto box = std::make_unique<DomNode>();
    box->type = DomNodeType::Element;
    box->name = "div";
    for (int i = 0; i < 2; ++i) {
        auto c = std::make_unique<DomNode>();
        c->type = DomNodeType::Element;
        c->name = "span";
        box->append_child(std::move(c));
    }
    root->append_child(std::move(box));
    CssParser parser;
    auto sheet = parser.parse(css);
    LayoutTreeBuilder builder;
    auto tree = builder.build(*root, sheet);
    LayoutEngine {}.layout(*tree, 100);
    return tree;
}

int main()
{
    // gap separates row items
    auto row = build("div { display:flex; gap:10; width:100; } span { width:20; height:10; }");
    auto& row_box = *row->children[0];
    require(row_box.children[1]->rect.x >= row_box.children[0]->rect.x + 30 - 0.01f, "row gap is applied");

    // flex-direction: column stacks vertically with a gap
    auto column = build("div { display:flex; flex-direction:column; gap:5; width:100; } span { height:10; }");
    auto& column_box = *column->children[0];
    require(column_box.children[1]->rect.y >= column_box.children[0]->rect.y + 15 - 0.01f,
        "column direction stacks with a gap");

    // flex-grow shares the remaining space
    auto grow = build("div { display:flex; width:100; } span { flex-grow:1; height:10; }");
    auto& grow_box = *grow->children[0];
    require(grow_box.children[0]->rect.width > 40 && grow_box.children[1]->rect.width > 40,
        "flex-grow expands both items");

    std::printf("flex layout: gap, column and grow verified\n");
    return 0;
}

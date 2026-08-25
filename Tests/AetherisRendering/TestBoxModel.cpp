#include <render_document.hpp>

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
    auto rendered = RenderDocument::create(
        "<div><p>Hello</p></div>",
        "div { width: 100px; margin: 10px 20px; padding: 5px; } p { height: 20px; margin: 3px; }",
        500);

    auto const& root = rendered.layout_root();
    auto const& div = *root.children[0];

    require(div.box.content.width == 100, "content width");
    require(div.box.margin.left == 20, "horizontal margin");
    require(div.box.padding.top == 5, "padding");
    require(div.rect.width == 110, "border box width with padding");
    require(div.children.size() == 1, "child layout");
    require(div.children[0]->rect.y >= div.box.content.y, "child positioned in content box");
    return 0;
}

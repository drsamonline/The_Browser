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
        "<div><span>One</span><span>Two</span><p>Three</p><aside></aside></div>",
        "div { width: 200px; min-width: 50%; max-width: 220px; }"
        "span { display: inline-block; width: 40px; height: 10px; }"
        "p { position: relative; left: 10px; top: 5px; width: 50%; }"
        "aside { position: absolute; left: 20px; top: 30px; width: 25%; height: 12px; }",
        400);

    auto const& div = *rendered.layout_root().children[0];
    require(div.box.content.width == 200, "explicit width wins");
    require(div.children[0]->display == LayoutDisplay::InlineBlock, "inline-block display");
    require(div.children[1]->rect.x >= div.children[0]->rect.x, "inline siblings laid out");
    require(div.children[2]->rect.x >= div.box.content.x + 10, "relative left offset");
    require(div.children[3]->rect.x >= div.box.content.x + 20, "absolute left offset");
    require(div.children[3]->box.content.width >= 49 && div.children[3]->box.content.width <= 51, "percentage absolute width");
    return 0;
}

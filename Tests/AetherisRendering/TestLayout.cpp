#include <render_document.hpp>

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
    auto rendered = RenderDocument::create(
        "<div><p>Hello</p><p>World</p></div>",
        "div { width: 400px; background-color: blue; } p { height: 20px; margin-top: 5px; color: red; }",
        800);

    auto const& root = rendered.layout_root();
    require(root.children.size() == 1, "document contains div layout node");

    auto const& div = *root.children[0];
    require(div.rect.width == 400, "CSS width applied");
    require(div.children.size() == 2, "paragraph layout children");
    require(div.children[1]->rect.y > div.children[0]->rect.y, "block children stack vertically");

    bool found_background = false;
    bool found_text = false;
    for (auto const& command : rendered.render_tree().commands()) {
        if (command.type == PaintCommand::Type::FillRect && command.color == "blue")
            found_background = true;
        if (command.type == PaintCommand::Type::DrawText && (command.text == "Hello" || command.text == "World"))
            found_text = true;
    }

    require(found_background, "background paint command");
    require(found_text, "text paint command");
    return 0;
}

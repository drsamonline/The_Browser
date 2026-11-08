#include <render_document.hpp>
#include <render_tree.hpp>

#include <cmath>
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

    // CSS transform is applied to the painted geometry. Each case renders the
    // same box with and without the transform, so the difference is the
    // transform alone rather than the flow position.
    {
        auto first_fill = [](std::string const& style) {
            auto rendered = RenderDocument::create(
                "<div style='width:50px;height:50px;background:#ff0000;" + style + "'></div>",
                "", 800.0f);
            auto tree = RenderTree::from_layout(rendered.layout_root());
            for (auto const& command : tree.commands())
                if (command.type == PaintCommand::Type::FillRect)
                    return command.rect;
            return LayoutRect {};
        };

        auto plain = first_fill("");
        auto translated = first_fill("transform:translate(100px,50px)");
        require(std::abs((translated.x - plain.x) - 100.0f) < 0.5f
                && std::abs((translated.y - plain.y) - 50.0f) < 0.5f,
            "translate offsets the painted box by the given amount");
        require(std::abs(translated.width - plain.width) < 0.5f, "translate does not resize");

        auto scaled = first_fill("transform:scale(2)");
        require(std::abs(scaled.width - 100.0f) < 0.5f && std::abs(scaled.height - 100.0f) < 0.5f,
            "scale multiplies the painted box");
    }

    return 0;
}

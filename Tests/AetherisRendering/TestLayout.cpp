#include <render_document.hpp>
#include <render_tree.hpp>

#include <cmath>
#include <cstdlib>
#include <functional>
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

// Row 3c1: the flex row algorithm - basis, shrink and the `flex` shorthand. Widths, not pixels: the question
    // is what the algorithm hands each item.
    {
        auto row_widths = [](std::string const& items_css) {
            std::string html = "<div class='row'><div class='i0'></div><div class='i1'></div><div class='i2'></div></div>";
            auto rendered = RenderDocument::create(html,
                ".row { display: flex; width: 300px; }"
                ".i0, .i1, .i2 { height: 10px; background: #f00; }" + items_css,
                300);
            std::vector<float> widths;
            std::function<void(LayoutNode const&)> walk = [&](LayoutNode const& node) {
                if (node.dom_node && node.dom_node->type == DomNodeType::Element && node.style.get("background-color"))
                    widths.push_back(node.rect.width);
                for (auto const& child : node.children)
                    walk(*child);
            };
            walk(rendered.layout_root());
            return widths;
        };

        auto const equal = row_widths(".i0,.i1,.i2 { flex: 1 }");
        require(equal.size() == 3, "three flex items");
        for (auto width : equal)
            require(std::abs(width - 100.0f) < 0.01f, "flex: 1 gives each item a third");

        auto const fixed_and_flex = row_widths(".i0 { width: 100px } .i1,.i2 { flex: 1 }");
        require(fixed_and_flex.size() == 3 && std::abs(fixed_and_flex[0] - 100.0f) < 0.01f
                && std::abs(fixed_and_flex[1] - 100.0f) < 0.01f && std::abs(fixed_and_flex[2] - 100.0f) < 0.01f,
            "a fixed item and two flexible ones split 300 px into three");

        auto const proportional = row_widths(".i0 { flex: 2 } .i1 { flex: 1 } .i2 { flex: 0 0 0px }");
        require(proportional.size() == 3 && std::abs(proportional[0] - 200.0f) < 0.01f
                && std::abs(proportional[1] - 100.0f) < 0.01f,
            "flex: 2 and flex: 1 split the free space two to one");

        auto const shrunk = row_widths(".i0,.i1 { flex-basis: 200px }");
        require(shrunk.size() == 3 && std::abs(shrunk[0] - 150.0f) < 0.01f && std::abs(shrunk[1] - 150.0f) < 0.01f,
            "two 200 px bases shrink to 150 each");

        auto const no_shrink = row_widths(".i0,.i1 { flex-basis: 200px; flex-shrink: 0 }");
        require(no_shrink.size() == 3 && std::abs(no_shrink[0] - 200.0f) < 0.01f
                && std::abs(no_shrink[1] - 200.0f) < 0.01f,
            "flex-shrink: 0 keeps its basis and overflows");

        auto const no_flex = row_widths(".i0,.i1,.i2 { flex: 0 0 150px }");
        require(no_flex.size() == 3, "three items");
        for (auto width : no_flex)
            require(std::abs(width - 150.0f) < 0.01f, "flex: 0 0 150px gives each item exactly 150 px");
    }

    return 0;
}

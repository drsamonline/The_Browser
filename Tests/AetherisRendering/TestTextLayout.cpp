#include <render_document.hpp>
#include <text_layout.hpp>

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
    require(TextLayout::collapse_whitespace("  hello \n\t world  ") == "hello world", "collapse whitespace");

    StyleProperties style;
    style.set("font-size", "10px");
    style.set("line-height", "2");
    auto fragments = TextLayout::layout("one two three four five", 0, 0, 25, style);
    require(fragments.size() >= 5, "word fragments");
    require(fragments[1].rect.y >= fragments[0].rect.y, "ordered lines");

    auto rendered = RenderDocument::create(
        "<div>Hello wonderful rendering engine world</div>",
        "div { width: 60px; font-size: 10px; line-height: 2; }",
        200);

    size_t text_commands = 0;
    for (auto const& command : rendered.render_tree().commands()) {
        if (command.type == PaintCommand::Type::DrawText) {
            ++text_commands;
            require(!command.text.empty(), "painted text");
            require(command.baseline >= command.rect.y, "baseline");
        }
    }
    require(text_commands > 1, "wrapped text creates fragments");
    return 0;
}

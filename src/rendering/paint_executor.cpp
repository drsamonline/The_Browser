#include "paint_executor.hpp"

namespace aetheris::rendering {

Color PaintExecutor::command_color(PaintCommand const& command)
{
    if (auto color = Color::parse(command.color))
        return *color;
    return { 0, 0, 0, 255 };
}

void PaintExecutor::execute(RenderTree const& tree, SoftwareSurface& surface) const
{
    ClipStack clips;

    for (auto const& command : tree.commands()) {
        if (command.push_clip) {
            clips.push(command.rect);
            continue;
        }
        if (command.pop_clip) {
            clips.pop();
            continue;
        }

        auto color = command_color(command);
        auto clip = clips.current();
        switch (command.type) {
        case PaintCommand::Type::FillRect:
            surface.fill_rect(command.rect, color, clip);
            break;
        case PaintCommand::Type::StrokeRect:
            surface.stroke_rect(command.rect, command.edges, color, clip);
            break;
        case PaintCommand::Type::DrawText:
            surface.fill_text_cell(command.rect, color, clip);
            break;
        }
    }
}

} // namespace aetheris::rendering

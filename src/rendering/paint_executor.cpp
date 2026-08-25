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
    for (auto const& command : tree.commands()) {
        auto color = command_color(command);
        switch (command.type) {
        case PaintCommand::Type::FillRect:
            surface.fill_rect(command.rect, color);
            break;
        case PaintCommand::Type::DrawText:
            surface.fill_text_cell(command.rect, color);
            break;
        }
    }
}

} // namespace aetheris::rendering

#include "paint_executor.hpp"

#include <algorithm>

namespace aetheris::rendering {

Color PaintExecutor::command_color(PaintCommand const& command)
{
    Color color = Color::parse(command.color).value_or(Color { 0, 0, 0, 255 });
    color.alpha = static_cast<uint8_t>(static_cast<float>(color.alpha) * std::clamp(command.opacity, 0.0f, 1.0f));
    return color;
}

void PaintExecutor::execute(RenderTree const& tree, SoftwareSurface& surface) const
{
    ClipStack clips;

    for (auto const& command : tree.commands()) {
        if (command.push_clip) {
            clips.push(command.rect, command.radius);
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
        case PaintCommand::Type::FillRoundedRect:
            surface.fill_rounded_rect(command.rect, command.radius, color, clip);
            break;
        case PaintCommand::Type::StrokeRect:
            surface.stroke_rect(command.rect, command.edges, color, clip, command.border_style);
            break;
        case PaintCommand::Type::StrokeRoundedRect:
            surface.stroke_rounded_rect(command.rect, command.radius, command.edges, color, clip, command.border_style);
            break;
        case PaintCommand::Type::DrawText:
            surface.fill_text_cell(command.rect, color, clip);
            break;
        case PaintCommand::Type::DrawImage:
            if (command.image) surface.draw_image(command.rect, *command.image, command.image_fit, clip);
            break;
        case PaintCommand::Type::DrawShadow:
            surface.draw_shadow(command.rect, command.shadow_offset_x, command.shadow_offset_y, command.shadow_blur, color, clip);
            break;
        case PaintCommand::Type::DrawOutline:
            surface.stroke_rect(command.rect, command.edges, color, clip, command.border_style);
            break;
        }
    }
}

} // namespace aetheris::rendering

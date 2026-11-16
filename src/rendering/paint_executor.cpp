#include "paint_executor.hpp"

#include <algorithm>

namespace aetheris::rendering {

namespace {

// Document coordinates to surface coordinates. Every rectangle the painter
// touches goes through here, including clip rectangles, or a clip would stay
// anchored to the document while its contents scrolled out from under it.
LayoutRect to_surface(LayoutRect rect, PaintOffset offset)
{
    rect.x -= offset.x;
    rect.y -= offset.y;
    return rect;
}

} // namespace

Color PaintExecutor::command_color(PaintCommand const& command)
{
    Color color = Color::parse(command.color).value_or(Color { 0, 0, 0, 255 });
    color.alpha = static_cast<uint8_t>(static_cast<float>(color.alpha) * std::clamp(command.opacity, 0.0f, 1.0f));
    return color;
}

void PaintExecutor::execute(RenderTree const& tree, SoftwareSurface& surface, PaintOffset offset) const
{
    ClipStack clips;

    for (auto const& command : tree.commands()) {
        if (command.push_clip) {
            clips.push(to_surface(command.rect, offset), command.radius);
            continue;
        }
        if (command.pop_clip) {
            clips.pop();
            continue;
        }

        // Commands entirely outside the surface are skipped before they reach
        // the rasteriser. A long page paints thousands of commands above and
        // below the viewport; culling them here is what keeps scrolling flat.
        auto rect = to_surface(command.rect, offset);
        if (rect.x + rect.width <= 0 || rect.y + rect.height <= 0
            || rect.x >= static_cast<float>(surface.width())
            || rect.y >= static_cast<float>(surface.height())) {
            continue;
        }

        auto color = command_color(command);
        auto clip = clips.current();
        switch (command.type) {
        case PaintCommand::Type::FillRect:
            surface.fill_rect(rect, color, clip);
            break;
        case PaintCommand::Type::FillRoundedRect:
            surface.fill_rounded_rect(rect, command.radius, color, clip);
            break;
        case PaintCommand::Type::StrokeRect:
            surface.stroke_rect(rect, command.edges, color, clip, command.border_style);
            break;
        case PaintCommand::Type::StrokeRoundedRect:
            surface.stroke_rounded_rect(rect, command.radius, command.edges, color, clip, command.border_style);
            break;
        case PaintCommand::Type::DrawText:
            surface.draw_text(rect, command.text, command.font_size, color, clip);
            break;
        case PaintCommand::Type::DrawTextDecoration:
            surface.draw_text_decoration(rect, command.text_decoration, color, clip);
            break;
        case PaintCommand::Type::DrawImage:
            if (command.image) {
                if (command.background_repeat != "no-repeat" || command.background_size != "auto" || command.background_position != "0 0")
                    surface.draw_background_image(rect, *command.image, command.background_repeat, command.background_position, command.background_size, clip);
                else
                    surface.draw_image(rect, *command.image, command.image_fit, clip);
            }
            break;
        case PaintCommand::Type::DrawShadow:
            surface.draw_shadow(rect, command.shadow_offset_x, command.shadow_offset_y, command.shadow_blur, color, clip);
            break;
        case PaintCommand::Type::DrawOutline:
            surface.stroke_rect(rect, command.edges, color, clip, command.border_style);
            break;
        case PaintCommand::Type::PushStackingContext:
        case PaintCommand::Type::PopStackingContext:
            break;
        }
    }
}

} // namespace aetheris::rendering

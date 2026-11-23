#include "paint_executor.hpp"
#include "font_library.hpp"

#include <algorithm>

namespace aetheris::rendering {

namespace {

// Document coordinates to surface coordinates. Every rectangle the painter
// touches goes through here, including clip rectangles, or a clip would stay
// anchored to the document while its contents scrolled out from under it.
// Where a `position: sticky` box belongs, given how far the page is scrolled. Positive pushes it down. A box
// with `top` only is never pushed up, one with `bottom` only is never pushed down, and either way it stops at
// the edges of its containing block (row 3b).
float sticky_shift(StickyRegion const& region, float scroll_y, float viewport_height)
{
    float const node_bottom = region.node_top + region.node_height;
    float const min_shift = region.containing_top - region.node_top;
    float const max_shift = region.containing_bottom - node_bottom;

    float shift = 0;
    if (region.has_top)
        shift = scroll_y + region.top - region.node_top;
    else if (region.has_bottom)
        shift = scroll_y + viewport_height - region.bottom - node_bottom;

    float const low = region.has_top ? 0.0f : min_shift;
    float const high = region.has_bottom ? 0.0f : max_shift;
    if (low <= high)
        shift = std::clamp(shift, low, high);
    else
        shift = std::clamp(shift, std::min(low, high), std::max(low, high));
    return shift;
}

LayoutRect to_surface(LayoutRect rect, PaintOffset offset, float sticky = 0.0f)
{
    rect.x -= offset.x;
    rect.y -= offset.y;
    rect.y += sticky;
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

    // One shift per command, so a sticky box and everything painted inside it move together and nothing else
    // does. Pages without a sticky box leave this empty and the loop is unchanged.
    std::vector<float> sticky;
    if (!tree.sticky_regions().empty()) {
        sticky.assign(tree.commands().size(), 0.0f);
        for (auto const& region : tree.sticky_regions()) {
            float const shift = sticky_shift(region, offset.y, static_cast<float>(surface.height()));
            for (std::size_t index = region.begin; index < region.end && index < sticky.size(); ++index)
                sticky[index] = shift;
        }
    }
    auto shift_at = [&](std::size_t index) { return sticky.empty() ? 0.0f : sticky[index]; };

    auto const& commands = tree.commands();
    for (std::size_t index = 0; index < commands.size(); ++index) {
        auto const& command = commands[index];
        if (command.push_clip) {
            clips.push(to_surface(command.rect, offset, shift_at(index)), command.radius);
            continue;
        }
        if (command.pop_clip) {
            clips.pop();
            continue;
        }

        // Commands entirely outside the surface are skipped before they reach
        // the rasteriser. A long page paints thousands of commands above and
        // below the viewport; culling them here is what keeps scrolling flat.
        auto rect = to_surface(command.rect, offset, shift_at(index));
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
            surface.draw_text(rect, command.text, command.font_size, color, clip, command.letter_spacing,
                command.word_spacing, font_for_family(command.font_family));
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

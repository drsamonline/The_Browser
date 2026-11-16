#include "scroll_bar.hpp"

#include <algorithm>

namespace aetheris::rendering {

ScrollBarGeometry ScrollBar::geometry(float viewport_width, float viewport_height, float content_height, float scroll_y)
{
    ScrollBarGeometry result;
    result.viewport_height = viewport_height;
    result.content_height = content_height;
    result.max_scroll = std::max(0.0f, content_height - viewport_height);

    if (viewport_height <= 0 || viewport_width <= 0 || result.max_scroll <= 0.5f)
        return result;

    result.visible = true;
    result.track = LayoutRect { viewport_width - thickness(), 0, thickness(), viewport_height };

    // Proportional thumb, floored so it stays grabbable on a very long page.
    float proportional = viewport_height * (viewport_height / content_height);
    float thumb_height = std::clamp(proportional, minimum_thumb_height(), viewport_height);
    float travel = std::max(0.0f, viewport_height - thumb_height);
    float progress = std::clamp(scroll_y / result.max_scroll, 0.0f, 1.0f);

    result.thumb = LayoutRect { result.track.x, travel * progress, thickness(), thumb_height };
    return result;
}

float ScrollBar::scroll_for_pointer(ScrollBarGeometry const& bar, float pointer_y)
{
    if (!bar.visible || bar.max_scroll <= 0)
        return 0;

    // Centre the thumb on the pointer, then clamp to the document.
    float travel = std::max(0.0f, bar.viewport_height - bar.thumb.height);
    if (travel <= 0)
        return 0;

    float top = pointer_y - bar.thumb.height / 2;
    return std::clamp(top / travel, 0.0f, 1.0f) * bar.max_scroll;
}

void ScrollBar::paint(SoftwareSurface& surface, ScrollBarGeometry const& bar, Color track_color, Color thumb_color)
{
    if (!bar.visible)
        return;
    surface.fill_rect(bar.track, track_color);
    surface.fill_rect(bar.thumb, thumb_color);
}

} // namespace aetheris::rendering

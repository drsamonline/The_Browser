#pragma once

#include "color.hpp"
#include "layout.hpp"
#include "software_surface.hpp"

namespace aetheris::rendering {

// A vertical scrollbar as pure geometry plus a paint step.
//
// The same numbers drive drawing and hit-testing, which is the only way a
// scrollbar thumb and a click on it stay in agreement as a page grows. Nothing
// here touches a window, so the whole thing is testable without a display.
struct ScrollBarGeometry {
    bool visible { false };
    LayoutRect track;
    LayoutRect thumb;
    float max_scroll { 0 };
    float viewport_height { 0 };
    float content_height { 0 };
};

class ScrollBar {
public:
    static constexpr float thickness() { return 10.0f; }
    static constexpr float minimum_thumb_height() { return 24.0f; }

    // `viewport_width`/`viewport_height` are the content area, in pixels.
    // Hidden when the document fits: a scrollbar over a short page is noise.
    static ScrollBarGeometry geometry(float viewport_width, float viewport_height, float content_height, float scroll_y);

    // The scroll offset that puts the thumb under `pointer_y`, for a click or
    // drag on the track. Clamped to the document, so it can never scroll past
    // the end.
    static float scroll_for_pointer(ScrollBarGeometry const&, float pointer_y);

    static void paint(SoftwareSurface&, ScrollBarGeometry const&, Color track_color, Color thumb_color);
};

} // namespace aetheris::rendering

#pragma once

#include "clip.hpp"
#include "render_tree.hpp"
#include "software_surface.hpp"

namespace aetheris::rendering {

// Where the viewport sits inside the document.
//
// Scrolling is a property of the paint pass, not of layout: the painter
// subtracts this offset from every rectangle, so moving down a long page never
// triggers a relayout. That is the whole reason a 20,000 px document scrolls as
// cheaply as a short one.
struct PaintOffset {
    float x { 0 };
    float y { 0 };
};

class PaintExecutor {
public:
    // `offset` is the viewport's scroll position in document coordinates.
    void execute(RenderTree const&, SoftwareSurface&, PaintOffset offset = {}) const;

private:
    static Color command_color(PaintCommand const&);
};

} // namespace aetheris::rendering

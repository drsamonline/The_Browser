#pragma once

#include "clip.hpp"
#include "render_tree.hpp"
#include "software_surface.hpp"

namespace aetheris::rendering {

class PaintExecutor {
public:
    void execute(RenderTree const&, SoftwareSurface&) const;

private:
    static Color command_color(PaintCommand const&);
};

} // namespace aetheris::rendering

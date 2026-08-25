#pragma once

#include "software_surface.hpp"

#include <string>

namespace aetheris::rendering {

class ImageOutput {
public:
    static bool write_ppm(SoftwareSurface const&, std::string const& path);
};

} // namespace aetheris::rendering

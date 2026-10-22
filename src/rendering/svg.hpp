#pragma once

#include "image.hpp"

#include <vector>

namespace aetheris::rendering {

// True if the bytes look like an SVG document.
bool is_svg(std::vector<unsigned char> const& bytes);

// Rasterises a subset of SVG (rect, circle, ellipse, line, polyline, polygon,
// path; fill/stroke; viewBox and translate/scale/matrix transforms) into an
// Image. When target_width/height are zero the document's intrinsic size is
// used. Returns false if the input is not SVG or has no drawable content.
bool render_svg(std::vector<unsigned char> const& bytes, Image& out, int target_width = 0, int target_height = 0);

} // namespace aetheris::rendering

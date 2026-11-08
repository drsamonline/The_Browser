#pragma once

#include "ttf_font.hpp"

#include <cstdint>
#include <vector>

namespace aetheris::rendering {

// An anti-aliased glyph bitmap. `left`/`top` are the offsets from the pen
// position to the bitmap's top-left corner, in pixels, with y growing down.
struct GlyphBitmap {
    int width { 0 };
    int height { 0 };
    int left { 0 };
    int top { 0 };
    std::vector<uint8_t> coverage; // width * height, 0 = empty, 255 = solid

    bool empty() const { return width <= 0 || height <= 0; }
    uint8_t at(int x, int y) const
    {
        if (x < 0 || y < 0 || x >= width || y >= height)
            return 0;
        return coverage[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
    }
};

// Rasterises a glyph outline at a pixel size. Quadratic curves are flattened
// and each scanline is sampled vertically, so edges are anti-aliased.
GlyphBitmap rasterize_glyph(TtfFont const& font, uint16_t glyph, float pixel_size);

} // namespace aetheris::rendering

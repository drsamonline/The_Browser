#pragma once

#include <cstdint>

namespace aetheris::rendering {

// One 8x8 monochrome glyph. Each byte is a row; bit 0x80 is the leftmost pixel.
struct BitmapGlyph {
    uint8_t rows[8];
};

// The embedded dependency-free bitmap font covers printable ASCII (0x20..0x7E).
// Anything outside that range falls back to a '?' glyph.
BitmapGlyph const& bitmap_glyph_for(char c);

// Horizontal advance and cell height, in pixels, for the embedded font at a
// given point size. Measurement and rasterisation both use these so glyphs line
// up exactly with the layout.
float bitmap_advance(float font_size);
float bitmap_cell_height(float font_size);

} // namespace aetheris::rendering

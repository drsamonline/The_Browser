#pragma once

#include "ttf_font.hpp"

namespace aetheris::rendering {

// The outline font used for text, when one can be found. Loaded once from
// AETHERIS_FONT or a short list of common system paths; null when no font is
// available, in which case the embedded bitmap font is used instead.
TtfFont const* active_font();

// Rasterised glyphs are cached by (glyph, pixel size): text drawing asks for
// the same glyphs repeatedly, and rasterising per draw call made a small page
// take seconds.
struct GlyphBitmap;
GlyphBitmap const& cached_glyph(TtfFont const& font, uint16_t glyph, float pixel_size);

// Kerning between two glyphs, in font units. Only the embedded font has a pair
// table; any other font returns 0.
int kerning_adjustment(TtfFont const& font, uint16_t left, uint16_t right);

} // namespace aetheris::rendering

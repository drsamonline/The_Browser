#pragma once

#include "ttf_font.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aetheris::rendering {

// The outline font used for text, when one can be found. Loaded once from
// AETHERIS_FONT or a short list of common system paths; null when no font is
// available, in which case the embedded bitmap font is used instead.
TtfFont const* active_font();

// A face registered from an `@font-face` rule. The bytes are copied into the font, which then lives for the
// process, exactly like `active_font()`; a family registered twice keeps the first.
bool register_font_face(std::string const& family, std::vector<uint8_t> const& data);

// The face a `font-family` names, or null when no `@font-face` declared it. Callers fall back to
// `active_font()` - the outline font the engine found for itself (row 3d1).
TtfFont const* font_for_family(std::string_view family);

// Rasterised glyphs are cached by (glyph, pixel size): text drawing asks for
// the same glyphs repeatedly, and rasterising per draw call made a small page
// take seconds.
struct GlyphBitmap;
GlyphBitmap const& cached_glyph(TtfFont const& font, uint16_t glyph, float pixel_size);

// Kerning between two glyphs, in font units. Only the embedded font has a pair
// table; any other font returns 0.
int kerning_adjustment(TtfFont const& font, uint16_t left, uint16_t right);

} // namespace aetheris::rendering

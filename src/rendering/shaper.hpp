#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace aetheris::rendering {

// One positioned glyph from a shaped run. `cluster` is the byte offset in the input text that this
// glyph came from, which is what a caret, a selection or a wrap opportunity needs - a code-point
// index is not enough once several characters become one glyph, or one character becomes several.
struct ShapedGlyph {
    uint32_t glyph { 0 };
    uint32_t cluster { 0 };
    float advance { 0 };
    float offset_x { 0 };
    float offset_y { 0 };
};

// Whether this build can shape at all. Without it, text is measured one code point at a time, which
// is wrong for Arabic, Indic scripts and any font ligature - the engine's original behaviour.
bool shaping_available();

// Shapes `text` with the font file in `font_data` at `size` pixels.
//
// `font_identity` names the font file for the shaper's caches: pass `TtfFont::identity()`, a content
// hash the font computes once when it loads. It is a parameter rather than something computed here
// because hashing the file on every call cost 1.23 ms of a 1.26 ms run at -O2 (row 2d4); pass 0 when
// the bytes belong to nothing that can supply one, and the run is shaped without either cache.
//
// Returns an empty vector when shaping is unavailable or the font cannot be read, so a caller can
// fall back to per-code-point advances without a second code path to test.
std::vector<ShapedGlyph> shape_run(std::vector<uint8_t> const& font_data, std::string_view text, float size, uint64_t font_identity);

} // namespace aetheris::rendering

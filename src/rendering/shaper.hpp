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
    // HarfBuzz's HB_GLYPH_FLAG_* for this glyph. UNSAFE_TO_BREAK means a line break at the start of this
    // glyph's cluster would change the shaped result - a ligature, a kerned pair, a mark that positions
    // against what precedes it - which is what safe_break_offsets() below reads.
    uint32_t flags { 0 };
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

// Byte offsets in `text` where a line may break without changing what is drawn: every cluster start that
// HarfBuzz does not flag HB_GLYPH_FLAG_UNSAFE_TO_BREAK, ascending, with the end of the text last.
//
// A combining mark shares its base's cluster and so never appears here; neither does a position inside a
// ligature ("office": clusters 0,1,4,5, so byte 2 is not offered) or between a kerned pair ("To": the
// break before "o" would change the spacing, so only the end is offered). Empty when there is no shaper
// or no font, and the caller then keeps the word whole.
std::vector<std::size_t> safe_break_offsets(std::vector<uint8_t> const& font_data, std::string_view text, float size, uint64_t font_identity);

} // namespace aetheris::rendering

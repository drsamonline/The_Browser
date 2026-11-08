#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aetheris::rendering {

// A point on a glyph outline, in font units. Quadratic off-curve points are
// control points between two on-curve points.
struct TtfPoint {
    float x { 0 };
    float y { 0 };
    bool on_curve { true };
};

struct TtfContour {
    std::vector<TtfPoint> points;
};

struct TtfOutline {
    std::vector<TtfContour> contours;
    bool empty() const { return contours.empty(); }
};

// A minimal TrueType/OpenType reader: enough to resolve a character to a glyph,
// read its advance, and extract its outline. It does not rasterise (that is the
// next step) and does not parse hinting, GPOS or GSUB.
class TtfFont {
public:
    bool load(std::vector<uint8_t> const& data);
    bool loaded() const { return !m_data.empty(); }
    std::string const& error() const { return m_error; }

    uint16_t units_per_em() const { return m_units_per_em; }
    uint16_t glyph_count() const { return m_num_glyphs; }
    int16_t ascender() const { return m_ascender; }
    int16_t descender() const { return m_descender; }

    // 0 (.notdef) when the codepoint is not mapped.
    uint16_t glyph_index(uint32_t codepoint) const;
    uint16_t advance_width(uint16_t glyph) const;

    // Outline in font units; empty for a blank glyph.
    TtfOutline outline(uint16_t glyph) const;

private:
    uint16_t u16(std::size_t offset) const;
    int16_t s16(std::size_t offset) const;
    uint32_t u32(std::size_t offset) const;
    std::size_t table(std::string const& tag) const;

    bool parse_cmap();
    bool parse_simple_glyph(std::size_t offset, uint16_t glyph, TtfOutline& out, int depth) const;

    std::vector<uint8_t> m_data;
    std::string m_error;

    std::size_t m_head { 0 };
    std::size_t m_hhea { 0 };
    std::size_t m_maxp { 0 };
    std::size_t m_hmtx { 0 };
    std::size_t m_loca { 0 };
    std::size_t m_glyf { 0 };
    std::size_t m_cmap_format4 { 0 };
    std::size_t m_cmap_format12 { 0 };

    uint16_t m_units_per_em { 1000 };
    uint16_t m_num_glyphs { 0 };
    uint16_t m_number_of_h_metrics { 0 };
    int16_t m_ascender { 0 };
    int16_t m_descender { 0 };
    int16_t m_index_to_loc_format { 0 };
};

} // namespace aetheris::rendering

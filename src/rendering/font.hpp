#pragma once
#include "style.hpp"
#include "ttf_font.hpp"
#include <cstdint>
#include <string_view>
namespace aetheris::rendering {

// Decodes one UTF-8 code point from `text` starting at `index`, advancing the index. Returns 0xFFFD for
// a malformed sequence, which is what every other text path in the platform substitutes.
//
// This exists because text used to be iterated a byte at a time: `for (unsigned char c : text)`. Every
// non-ASCII character was therefore measured and drawn as two to four separate Latin-1 glyphs, so "café"
// was six glyphs wide instead of four and any Devanagari or CJK text came out as mojibake.
inline uint32_t utf8_next(std::string_view text, std::size_t& index)
{
    if (index >= text.size())
        return 0;
    auto const first = static_cast<unsigned char>(text[index]);
    std::size_t extra = 0;
    uint32_t code_point = 0;
    if (first < 0x80) {
        ++index;
        return first;
    } else if ((first & 0xE0) == 0xC0) {
        extra = 1;
        code_point = first & 0x1F;
    } else if ((first & 0xF0) == 0xE0) {
        extra = 2;
        code_point = first & 0x0F;
    } else if ((first & 0xF8) == 0xF0) {
        extra = 3;
        code_point = first & 0x07;
    } else {
        ++index;
        return 0xFFFD;
    }
    if (index + extra >= text.size() + 0 && index + extra > text.size() - 1) {
        // Truncated sequence: consume what is there.
        index = text.size();
        return 0xFFFD;
    }
    for (std::size_t offset = 1; offset <= extra; ++offset) {
        auto const continuation = static_cast<unsigned char>(text[index + offset]);
        if ((continuation & 0xC0) != 0x80) {
            ++index;
            return 0xFFFD;
        }
        code_point = (code_point << 6) | (continuation & 0x3F);
    }
    index += extra + 1;
    return code_point;
}

struct FontMetrics { float size{16};
    // The face this size belongs to: the one `font-family` named, or the engine's own. Null only when the
    // build has no font at all, in which case the bitmap font draws instead (row 3d1).
    TtfFont const* face{nullptr}; float ascent{12.8f}; float descent{3.2f}; float line_gap{3.2f}; float advance(char) const;
    // The real metric. `advance(char)` is the ASCII shortcut and delegates here.
    float advance_for_code_point(uint32_t code_point) const; float line_height() const{return ascent+descent+line_gap;} }; class Font { public: static Font from_style(StyleProperties const&);
    TtfFont const* face() const{return m_metrics.face;} FontMetrics const& metrics() const{return m_metrics;} float measure(std::string_view text, float letter_spacing = 0, float word_spacing = 0) const; private: FontMetrics m_metrics; float m_weight{400}; }; }

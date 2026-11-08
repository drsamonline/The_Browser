#include "ttf_font.hpp"

#include <cstring>

namespace aetheris::rendering {

namespace {

constexpr uint32_t tag_of(char a, char b, char c, char d)
{
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16)
        | (static_cast<uint32_t>(c) << 8) | static_cast<uint32_t>(d);
}

} // namespace

uint16_t TtfFont::u16(std::size_t offset) const
{
    if (offset + 2 > m_data.size())
        return 0;
    return static_cast<uint16_t>((m_data[offset] << 8) | m_data[offset + 1]);
}

int16_t TtfFont::s16(std::size_t offset) const
{
    return static_cast<int16_t>(u16(offset));
}

uint32_t TtfFont::u32(std::size_t offset) const
{
    if (offset + 4 > m_data.size())
        return 0;
    return (static_cast<uint32_t>(m_data[offset]) << 24)
        | (static_cast<uint32_t>(m_data[offset + 1]) << 16)
        | (static_cast<uint32_t>(m_data[offset + 2]) << 8)
        | static_cast<uint32_t>(m_data[offset + 3]);
}

// Offset of a table's data, or 0 when it is absent.
std::size_t TtfFont::table(std::string const& name) const
{
    if (m_data.size() < 12 || name.size() != 4)
        return 0;
    auto wanted = tag_of(name[0], name[1], name[2], name[3]);
    auto count = u16(4);
    for (uint16_t index = 0; index < count; ++index) {
        auto record = 12 + static_cast<std::size_t>(index) * 16;
        if (record + 16 > m_data.size())
            break;
        if (u32(record) == wanted)
            return u32(record + 8);
    }
    return 0;
}

bool TtfFont::load(std::vector<uint8_t> const& data)
{
    m_data = data;
    m_error.clear();
    if (m_data.size() < 12) {
        m_error = "file too small to be a font";
        m_data.clear();
        return false;
    }

    auto version = u32(0);
    if (version != 0x00010000 && version != tag_of('t', 'r', 'u', 'e') && version != tag_of('O', 'T', 'T', 'O')) {
        m_error = "unrecognised font signature";
        m_data.clear();
        return false;
    }

    m_head = table("head");
    m_hhea = table("hhea");
    m_maxp = table("maxp");
    m_hmtx = table("hmtx");
    m_loca = table("loca");
    m_glyf = table("glyf");
    if (!m_head || !m_hhea || !m_maxp) {
        m_error = "missing required tables (head/hhea/maxp)";
        m_data.clear();
        return false;
    }

    m_units_per_em = u16(m_head + 18);
    m_index_to_loc_format = s16(m_head + 50);
    m_ascender = s16(m_hhea + 4);
    m_descender = s16(m_hhea + 6);
    m_number_of_h_metrics = u16(m_hhea + 34);
    m_num_glyphs = u16(m_maxp + 4);
    if (m_units_per_em == 0)
        m_units_per_em = 1000;

    if (!parse_cmap()) {
        m_error = "no usable cmap subtable";
        m_data.clear();
        return false;
    }
    return true;
}

bool TtfFont::parse_cmap()
{
    auto cmap = table("cmap");
    if (!cmap)
        return false;
    auto count = u16(cmap + 2);
    for (uint16_t index = 0; index < count; ++index) {
        auto record = cmap + 4 + static_cast<std::size_t>(index) * 8;
        auto platform = u16(record);
        auto encoding = u16(record + 2);
        auto subtable = cmap + u32(record + 4);
        auto format = u16(subtable);
        bool unicode = platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10));
        if (!unicode)
            continue;
        if (format == 4 && !m_cmap_format4)
            m_cmap_format4 = subtable;
        else if (format == 12 && !m_cmap_format12)
            m_cmap_format12 = subtable;
    }
    return m_cmap_format4 != 0 || m_cmap_format12 != 0;
}

uint16_t TtfFont::glyph_index(uint32_t codepoint) const
{
    // Format 12 covers the full range; prefer it when present.
    if (m_cmap_format12) {
        auto groups = u32(m_cmap_format12 + 12);
        for (uint32_t index = 0; index < groups; ++index) {
            auto group = m_cmap_format12 + 16 + static_cast<std::size_t>(index) * 12;
            auto start = u32(group);
            auto end = u32(group + 4);
            if (codepoint >= start && codepoint <= end)
                return static_cast<uint16_t>(u32(group + 8) + (codepoint - start));
        }
        return 0;
    }

    if (!m_cmap_format4 || codepoint > 0xFFFF)
        return 0;

    auto segments = u16(m_cmap_format4 + 6) / 2;
    auto end_codes = m_cmap_format4 + 14;
    auto start_codes = end_codes + segments * 2 + 2;
    auto id_deltas = start_codes + segments * 2;
    auto id_range_offsets = id_deltas + segments * 2;

    for (uint16_t index = 0; index < segments; ++index) {
        auto end = u16(end_codes + index * 2);
        if (codepoint > end)
            continue;
        auto start = u16(start_codes + index * 2);
        if (codepoint < start)
            return 0;
        auto delta = s16(id_deltas + index * 2);
        auto range_offset = u16(id_range_offsets + index * 2);
        if (range_offset == 0)
            return static_cast<uint16_t>((codepoint + delta) & 0xFFFF);
        auto glyph_address = id_range_offsets + index * 2 + range_offset + (codepoint - start) * 2;
        auto glyph = u16(glyph_address);
        if (glyph == 0)
            return 0;
        return static_cast<uint16_t>((glyph + delta) & 0xFFFF);
    }
    return 0;
}

uint16_t TtfFont::advance_width(uint16_t glyph) const
{
    if (!m_hmtx || m_number_of_h_metrics == 0)
        return 0;
    // Glyphs past numberOfHMetrics reuse the last advance.
    auto index = glyph < m_number_of_h_metrics ? glyph : static_cast<uint16_t>(m_number_of_h_metrics - 1);
    return u16(m_hmtx + static_cast<std::size_t>(index) * 4);
}

bool TtfFont::parse_simple_glyph(std::size_t offset, uint16_t glyph, TtfOutline& out, int depth) const
{
    if (depth > 4 || offset + 10 > m_data.size())
        return false;
    auto contours = s16(offset);
    if (contours == 0)
        return true; // blank glyph
    if (contours < 0)
        return false; // composite glyphs are handled by the caller

    auto end_points = offset + 10;
    auto point_count = static_cast<std::size_t>(u16(end_points + (contours - 1) * 2)) + 1;
    if (point_count > 4096)
        return false;

    auto instruction_length_at = end_points + static_cast<std::size_t>(contours) * 2;
    auto flags_at = instruction_length_at + 2 + u16(instruction_length_at);
    if (flags_at > m_data.size())
        return false;

    std::vector<uint8_t> flags;
    flags.reserve(point_count);
    std::size_t position = flags_at;
    while (flags.size() < point_count && position < m_data.size()) {
        auto flag = m_data[position++];
        flags.push_back(flag);
        if (flag & 0x08) { // repeat
            if (position >= m_data.size())
                break;
            auto repeat = m_data[position++];
            for (uint8_t r = 0; r < repeat && flags.size() < point_count; ++r)
                flags.push_back(flag);
        }
    }
    if (flags.size() < point_count)
        return false;

    std::vector<int32_t> xs(point_count, 0);
    std::vector<int32_t> ys(point_count, 0);

    int32_t value = 0;
    for (std::size_t index = 0; index < point_count; ++index) {
        auto flag = flags[index];
        if (flag & 0x02) { // x is a short delta
            if (position >= m_data.size())
                return false;
            int32_t delta = m_data[position++];
            value += (flag & 0x10) ? delta : -delta;
        } else if (!(flag & 0x10)) { // x is a 16-bit delta
            value += s16(position);
            position += 2;
        }
        xs[index] = value;
    }
    value = 0;
    for (std::size_t index = 0; index < point_count; ++index) {
        auto flag = flags[index];
        if (flag & 0x04) {
            if (position >= m_data.size())
                return false;
            int32_t delta = m_data[position++];
            value += (flag & 0x20) ? delta : -delta;
        } else if (!(flag & 0x20)) {
            value += s16(position);
            position += 2;
        }
        ys[index] = value;
    }

    std::size_t start = 0;
    for (int16_t contour = 0; contour < contours; ++contour) {
        auto end = static_cast<std::size_t>(u16(end_points + contour * 2));
        if (end >= point_count)
            return false;
        TtfContour points;
        for (std::size_t index = start; index <= end; ++index) {
            TtfPoint point;
            point.x = static_cast<float>(xs[index]);
            point.y = static_cast<float>(ys[index]);
            point.on_curve = (flags[index] & 0x01) != 0;
            points.points.push_back(point);
        }
        out.contours.push_back(std::move(points));
        start = end + 1;
    }
    (void)glyph;
    return true;
}

TtfOutline TtfFont::outline(uint16_t glyph) const
{
    TtfOutline result;
    if (!m_loca || !m_glyf || glyph >= m_num_glyphs)
        return result;

    std::size_t start = 0;
    std::size_t end = 0;
    if (m_index_to_loc_format == 0) {
        start = static_cast<std::size_t>(u16(m_loca + static_cast<std::size_t>(glyph) * 2)) * 2;
        end = static_cast<std::size_t>(u16(m_loca + static_cast<std::size_t>(glyph + 1) * 2)) * 2;
    } else {
        start = u32(m_loca + static_cast<std::size_t>(glyph) * 4);
        end = u32(m_loca + static_cast<std::size_t>(glyph + 1) * 4);
    }
    if (end <= start)
        return result; // no outline (space, for example)

    parse_simple_glyph(m_glyf + start, glyph, result, 0);
    return result;
}

} // namespace aetheris::rendering

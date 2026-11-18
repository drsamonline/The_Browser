#include "shaper.hpp"

#ifdef AETHERIS_HAVE_HARFBUZZ
#include <hb-ot.h>
#include <hb.h>
#endif

namespace aetheris::rendering {

#ifdef AETHERIS_HAVE_HARFBUZZ

namespace {
// A face per font file, kept alive because creating one per run re-parses the font. Keyed by the
// data pointer and size, which is stable for a loaded TtfFont.
struct CachedFace {
    hb_blob_t* blob { nullptr };
    hb_face_t* face { nullptr };
    hb_font_t* font { nullptr };
    float size { 0 };
};
} // namespace

bool shaping_available() { return true; }

std::vector<ShapedGlyph> shape_run(std::vector<uint8_t> const& font_data, std::string_view text, float size)
{
    if (font_data.empty() || text.empty())
        return {};

    // NB: one face per call for now. Caching belongs here, but a cache keyed on the data pointer is
    // wrong when a Font is reloaded into the same buffer, so it needs the font's identity, not its
    // address - and that is a change to TtfFont, not to this file.
    hb_blob_t* blob = hb_blob_create(reinterpret_cast<char const*>(font_data.data()),
        static_cast<unsigned>(font_data.size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    hb_face_t* face = hb_face_create(blob, 0);
    hb_font_t* font = hb_font_create(face);
    hb_ot_font_set_funcs(font);

    unsigned upem = hb_face_get_upem(face);
    if (upem == 0)
        upem = 1000;
    // HarfBuzz positions in font units scaled by this; ask for the size in 26.6 fixed point.
    int const scale = static_cast<int>(size * 64.0f);
    hb_font_set_scale(font, scale, scale);

    hb_buffer_t* buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, text.data(), static_cast<int>(text.size()), 0, static_cast<int>(text.size()));
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(font, buffer, nullptr, 0);

    unsigned count = hb_buffer_get_length(buffer);
    hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buffer, nullptr);
    hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, nullptr);

    std::vector<ShapedGlyph> glyphs;
    glyphs.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        glyphs.push_back(ShapedGlyph {
            info[i].codepoint,
            info[i].cluster,
            static_cast<float>(positions[i].x_advance) / 64.0f,
            static_cast<float>(positions[i].x_offset) / 64.0f,
            static_cast<float>(-positions[i].y_offset) / 64.0f,
        });
    }

    hb_buffer_destroy(buffer);
    hb_font_destroy(font);
    hb_face_destroy(face);
    hb_blob_destroy(blob);
    return glyphs;
}

#else

bool shaping_available() { return false; }

std::vector<ShapedGlyph> shape_run(std::vector<uint8_t> const&, std::string_view, float) { return {}; }

#endif

} // namespace aetheris::rendering

#include "shaper.hpp"

#include <cstdint>
#include <map>
#include <vector>

#ifdef AETHERIS_HAVE_HARFBUZZ
#include <hb-ot.h>
#include <hb.h>
#endif

namespace aetheris::rendering {

#ifdef AETHERIS_HAVE_HARFBUZZ

namespace {
// A face per font file. Creating one per run was measurable: `hb_face_create` sanitises the table
// directory, and text is measured many times per page. Keyed by a hash of the font data rather than
// its address, because an address is reused when a font is reloaded into the same buffer - a cache
// keyed on the pointer would then serve the previous font's glyphs.
struct FaceEntry {
    hb_blob_t* blob { nullptr };
    hb_face_t* face { nullptr };
};

uint64_t hash_font_data(std::vector<uint8_t> const& data)
{
    uint64_t hash = 1469598103934665603ull;
    for (auto byte : data) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::map<uint64_t, FaceEntry>& face_cache()
{
    static std::map<uint64_t, FaceEntry> cache;
    return cache;
}

hb_face_t* cached_face(std::vector<uint8_t> const& data)
{
    uint64_t const key = hash_font_data(data);
    auto& cache = face_cache();
    auto found = cache.find(key);
    if (found != cache.end())
        return found->second.face;
    // The blob copies the bytes, so the cached face does not depend on the caller's buffer outliving it.
    hb_blob_t* blob = hb_blob_create(reinterpret_cast<char const*>(data.data()),
        static_cast<unsigned>(data.size()), HB_MEMORY_MODE_DUPLICATE, nullptr, nullptr);
    hb_face_t* face = hb_face_create(blob, 0);
    cache.emplace(key, FaceEntry { blob, face });
    return face;
}
} // namespace

bool shaping_available() { return true; }

std::vector<ShapedGlyph> shape_run(std::vector<uint8_t> const& font_data, std::string_view text, float size)
{
    if (font_data.empty() || text.empty())
        return {};

    hb_face_t* face = cached_face(font_data);
    if (!face)
        return {};
    // A font per call: it carries the scale, and a cached one would need a second key for the size.
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
    // The face and blob are owned by the cache and live for the process.
    return glyphs;
}

#else

bool shaping_available() { return false; }

std::vector<ShapedGlyph> shape_run(std::vector<uint8_t> const&, std::string_view, float) { return {}; }

#endif

} // namespace aetheris::rendering

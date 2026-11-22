#include "shaper.hpp"

#include "bidi.hpp"

#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef AETHERIS_HAVE_HARFBUZZ
#include <hb-ot.h>
#include <hb.h>
#endif

namespace aetheris::rendering {

#ifdef AETHERIS_HAVE_HARFBUZZ

namespace {

// A face per font file. Creating one per run was measurable: `hb_face_create` sanitises the table
// directory, and text is measured many times per page.
//
// The key is `TtfFont::identity()` - a content hash the font computes once when it loads - and not
// the buffer's address, which is reused when a font is reloaded into the same buffer and would then
// serve the previous font's glyphs. It is also not a hash taken here: hashing the 756 KB file on
// every shaped run cost 1.23 ms of a 1.26 ms run at -O2, 98% of the total (row 2d4, tools/shaping-bench.cpp).
struct FaceEntry {
    hb_blob_t* blob { nullptr };
    hb_face_t* face { nullptr };
};

std::map<uint64_t, FaceEntry>& face_cache()
{
    static std::map<uint64_t, FaceEntry> cache;
    return cache;
}

hb_face_t* cached_face(std::vector<uint8_t> const& data, uint64_t identity)
{
    auto& cache = face_cache();
    if (auto found = cache.find(identity); found != cache.end())
        return found->second.face;
    // The blob copies the bytes, so the cached face does not depend on the caller's buffer outliving it.
    hb_blob_t* blob = hb_blob_create(reinterpret_cast<char const*>(data.data()),
        static_cast<unsigned>(data.size()), HB_MEMORY_MODE_DUPLICATE, nullptr, nullptr);
    hb_face_t* face = hb_face_create(blob, 0);
    cache.emplace(identity, FaceEntry { blob, face });
    return face;
}

hb_font_t* make_scaled_font(hb_face_t* face, float size)
{
    hb_font_t* font = hb_font_create(face);
    hb_ot_font_set_funcs(font);
    // HarfBuzz positions in font units scaled by this; ask for the size in 26.6 fixed point. The
    // font carries the scale, so it belongs to the size and is cached with it.
    int const scale = static_cast<int>(size * 64.0f);
    hb_font_set_scale(font, scale, scale);
    return font;
}

// What depends on the size: the hb_font_t above, and the shaped runs, because a run's advances are in
// pixels. Upstream keeps the same shape one level up - `Font::shaping_cache()` on a Font that already
// carries its size, keyed by text plus the style bits that change the result
// (Libraries/LibGfx/Font/Font.h, `ShapingCacheKey`). Our shaper is a free function rather than a method
// on a sized Font, so the font and the size go in the key here instead.
struct SizeCache {
    hb_font_t* font { nullptr };
    // Heterogeneous lookup: a cache hit compares the caller's string_view and does not allocate a key.
    std::map<std::string, std::vector<ShapedGlyph>, std::less<>> runs;
};

std::map<std::pair<uint64_t, uint32_t>, SizeCache>& size_caches()
{
    static std::map<std::pair<uint64_t, uint32_t>, SizeCache> caches;
    return caches;
}

size_t& cached_run_count()
{
    static size_t count = 0;
    return count;
}

// Upstream leaves its cache unbounded and says so (LibGfx/Font/Font.h: "FIXME: The cache currently
// grows unbounded. We should have some limit and LRU mechanism."). A page has far fewer distinct runs
// than this; a document that exceeds it drops everything and re-shapes once per run, which keeps
// memory flat without an LRU thread through the hit path.
constexpr size_t kMaxCachedRuns = 4096;

uint32_t bits_of(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// Shapes one run into `out`. Clusters stay byte offsets into the whole text because the buffer is told
// the item's offset within it: a caller that stitches runs together still sees one coordinate space.
void shape_into(hb_font_t* font, std::string_view text, std::size_t begin, std::size_t length,
    std::optional<hb_direction_t> direction, std::vector<ShapedGlyph>& out)
{
    hb_buffer_t* buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, text.data(), static_cast<int>(text.size()), static_cast<unsigned>(begin),
        static_cast<int>(length));
    // A forced direction wins: hb_buffer_guess_segment_properties() only fills in what is unset, so
    // setting the direction first is how a run says which way it reads. It still supplies the script.
    if (direction)
        hb_buffer_set_direction(buffer, *direction);
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(font, buffer, nullptr, 0);

    unsigned count = hb_buffer_get_length(buffer);
    hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buffer, nullptr);
    hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, nullptr);
    out.reserve(out.size() + count);
    for (unsigned i = 0; i < count; ++i) {
        out.push_back(ShapedGlyph {
            info[i].codepoint,
            info[i].cluster,
            static_cast<float>(positions[i].x_advance) / 64.0f,
            static_cast<float>(positions[i].x_offset) / 64.0f,
            static_cast<float>(-positions[i].y_offset) / 64.0f,
            hb_glyph_info_get_glyph_flags(&info[i]),
        });
    }
    hb_buffer_destroy(buffer);
}

} // namespace

bool shaping_available() { return true; }

std::vector<ShapedGlyph> shape_run(std::vector<uint8_t> const& font_data, std::string_view text, float size, uint64_t font_identity)
{
    if (font_data.empty() || text.empty())
        return {};

    uint32_t const size_bits = bits_of(size);
    SizeCache* cache = font_identity != 0 ? &size_caches()[{ font_identity, size_bits }] : nullptr;

    if (cache) {
        if (auto found = cache->runs.find(text); found != cache->runs.end())
            return found->second;
    }

    // Without an identity there is nothing to key a face or a run on, so the face is created for this
    // call and destroyed with it. Correct, just not cheap; every caller in the engine has an identity.
    hb_blob_t* owned_blob = nullptr;
    hb_face_t* face = nullptr;
    hb_font_t* font = nullptr;
    if (cache) {
        face = cached_face(font_data, font_identity);
        if (!cache->font)
            cache->font = make_scaled_font(face, size);
        font = cache->font;
    } else {
        owned_blob = hb_blob_create(reinterpret_cast<char const*>(font_data.data()),
            static_cast<unsigned>(font_data.size()), HB_MEMORY_MODE_DUPLICATE, nullptr, nullptr);
        face = hb_face_create(owned_blob, 0);
        font = make_scaled_font(face, size);
    }

    if (!face || !font) {
        if (!cache) {
            hb_font_destroy(font);
            hb_face_destroy(face);
            hb_blob_destroy(owned_blob);
        }
        return {};
    }

    // One buffer per directional run, concatenated in logical order. A string with no right-to-left
    // code point takes the single-run path it always took; `may_require_bidi` is what makes that free
    // for Latin text, and it is checked here rather than by the caller so measure and paint cannot
    // disagree about it.
    std::vector<ShapedGlyph> glyphs;
    auto const runs = split_directional_runs(text);
    if (runs.empty()) {
        shape_into(font, text, 0, text.size(), std::nullopt, glyphs);
    } else {
        for (auto const& run : runs)
            shape_into(font, text, run.begin, run.end - run.begin,
                run.right_to_left ? HB_DIRECTION_RTL : HB_DIRECTION_LTR, glyphs);
    }

    if (!cache) {
        hb_font_destroy(font);
        hb_face_destroy(face);
        hb_blob_destroy(owned_blob);
        return glyphs;
    }

    // The face and the font are owned by the caches and live for the process. An empty run is a
    // failure (no font, no glyphs), and caching a failure would make it permanent.
    if (!glyphs.empty()) {
        if (cached_run_count() >= kMaxCachedRuns) {
            for (auto& [key, entry] : size_caches()) {
                if (entry.font)
                    hb_font_destroy(entry.font);
            }
            size_caches().clear();
            cached_run_count() = 0;
            cache = &size_caches()[{ font_identity, size_bits }];
        }
        cache->runs.emplace(std::string(text), glyphs);
        ++cached_run_count();
    }
    return glyphs;
}

std::vector<std::size_t> safe_break_offsets(std::vector<uint8_t> const& font_data, std::string_view text, float size, uint64_t font_identity)
{
    std::vector<std::size_t> offsets;
    if (text.empty())
        return offsets;

    auto const glyphs = shape_run(font_data, text, size, font_identity);
    if (glyphs.empty())
        return offsets; // no shaper or no font: the caller keeps the word whole

    // Per cluster rather than per glyph, because the flag means "a break at the start of this cluster is
    // unsafe" and one cluster can be several glyphs. A cluster is unsafe if any of its glyphs says so.
    std::map<std::size_t, bool> unsafe_by_cluster;
    for (auto const& glyph : glyphs)
        unsafe_by_cluster[glyph.cluster] |= (glyph.flags & HB_GLYPH_FLAG_UNSAFE_TO_BREAK) != 0;

    // A std::map iterates in key order, so the offsets come out ascending whatever order the glyphs were
    // in - which matters because an RTL run arrives in visual order, i.e. descending.
    for (auto const& [cluster, unsafe] : unsafe_by_cluster) {
        if (cluster > 0 && !unsafe)
            offsets.push_back(cluster);
    }
    offsets.push_back(text.size());
    return offsets;
}

#else

bool shaping_available() { return false; }

std::vector<ShapedGlyph> shape_run(std::vector<uint8_t> const&, std::string_view, float, uint64_t) { return {}; }

std::vector<std::size_t> safe_break_offsets(std::vector<uint8_t> const&, std::string_view, float, uint64_t) { return {}; }

#endif

} // namespace aetheris::rendering

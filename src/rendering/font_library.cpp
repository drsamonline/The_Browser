#include "font_library.hpp"

#include "glyph_rasterizer.hpp"

#include <map>
#include <utility>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "embedded_font.inc"
#include "embedded_kern.inc"

namespace aetheris::rendering {

namespace {

std::vector<uint8_t> read_file(std::string const& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

TtfFont* load_font()
{
    auto* from_env = std::getenv("AETHERIS_FONT");
    // "none" forces the embedded bitmap font, which is what a font-free machine
    // and the test suite use.
    if (from_env && std::string(from_env) == "none")
        return nullptr;

    // An explicit path wins over everything.
    if (from_env) {
        auto data = read_file(from_env);
        if (!data.empty()) {
            auto* font = new TtfFont;
            if (font->load(data))
                return font;
            delete font;
        }
    }

    // The embedded subset is the default, so the binary renders identically on a
    // machine with no fonts installed.
    {
        auto* font = new TtfFont;
        std::vector<uint8_t> bytes(kEmbeddedFontData, kEmbeddedFontData + kEmbeddedFontSize);
        if (font->load(bytes))
            return font;
        delete font;
    }

    // Last resort: a system font, in case the embedded data ever fails to parse.
    std::vector<std::string> candidates {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/local/lib/python3.12/site-packages/matplotlib/mpl-data/fonts/ttf/DejaVuSans.ttf",
    };
    for (auto const& path : candidates) {
        auto data = read_file(path);
        if (data.empty())
            continue;
        auto* font = new TtfFont;
        if (font->load(data))
            return font;
        delete font;
    }
    return nullptr;
}

} // namespace

TtfFont const* active_font()
{
    static TtfFont* font = load_font();
    return font;
}

int kerning_adjustment(TtfFont const& font, uint16_t left, uint16_t right)
{
    // The pair table belongs to the embedded subset's glyph ids, so it only
    // applies when that font is the active one.
    if (&font != active_font())
        return 0;
    unsigned long low = 0;
    unsigned long high = kEmbeddedKernPairCount;
    while (low < high) {
        auto middle = (low + high) / 2;
        auto const& pair = kEmbeddedKernPairs[middle];
        if (pair.left == left && pair.right == right)
            return pair.value;
        if (pair.left < left || (pair.left == left && pair.right < right))
            low = middle + 1;
        else
            high = middle;
    }
    return 0;
}

} // namespace aetheris::rendering

namespace aetheris::rendering {

GlyphBitmap const& cached_glyph(TtfFont const& font, uint16_t glyph, float pixel_size)
{
    // Key on the rounded size so 16px and 16.2px share an entry.
    auto size_key = static_cast<int>(pixel_size * 4.0f + 0.5f);
    static std::map<std::pair<int, int>, GlyphBitmap> cache;
    auto key = std::make_pair(static_cast<int>(glyph), size_key);
    auto found = cache.find(key);
    if (found != cache.end())
        return found->second;
    auto inserted = cache.emplace(key, rasterize_glyph(font, glyph, static_cast<float>(size_key) / 4.0f));
    return inserted.first->second;
}

} // namespace aetheris::rendering

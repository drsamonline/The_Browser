#include "font_library.hpp"

#include "glyph_rasterizer.hpp"

#include <map>
#include <tuple>
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

namespace {

std::string trim_copy(std::string const& value)
{
    std::size_t begin = 0, end = value.size();
    while (begin < end && (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\'' || value[begin] == '"'))
        ++begin;
    while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\'' || value[end - 1] == '"'))
        --end;
    return value.substr(begin, end - begin);
}

std::map<std::string, TtfFont*>& face_registry()
{
    static std::map<std::string, TtfFont*> faces;
    return faces;
}

} // namespace

bool register_font_face(std::string const& family, std::vector<uint8_t> const& data)
{
    if (family.empty() || data.empty())
        return false;
    auto& faces = face_registry();
    if (faces.find(family) != faces.end())
        return true;
    auto* font = new TtfFont;
    if (!font->load(data)) {
        delete font;
        return false;
    }
    faces.emplace(family, font);
    return true;
}

namespace {

// A generic family has no face of its own. A browser asks fontconfig; this engine has no fontconfig, so it tries
// the system paths a Debian desktop uses, in order, and falls back to the embedded face only if none exists.
//
// This matters more than it looks. The embedded face is DejaVu Sans, and the browser's default `sans-serif` on
// this machine is Noto Sans: the same string measures **440px in DejaVu and 385px in Noto**, so every line of a
// page set in `sans-serif` held 14% fewer words and broke early (row 0.22).
char const* system_font_path(std::string const& name)
{
    struct Generic {
        char const* family;
        char const* paths[3];
    };
    static Generic const generics[] = {
        { "sans-serif",
            { "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
                "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", nullptr } },
        { "system-ui",
            { "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
                "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", nullptr } },
        { "serif",
            { "/usr/share/fonts/truetype/noto/NotoSerif-Regular.ttf",
                "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf", nullptr } },
        { "monospace",
            { "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf", nullptr } },
    };
    for (auto const& generic : generics) {
        if (name != generic.family)
            continue;
        for (auto const* path : generic.paths) {
            if (!path)
                break;
            if (std::ifstream probe(path); probe)
                return path;
        }
        return nullptr;
    }
    return nullptr;
}

} // namespace

TtfFont const* font_for_family(std::string_view family)
{
    if (family.empty())
        return nullptr;
    // `font-family: "Body", serif` - the first name that has a face wins, and a name with no face falls through
    // to the engine's own font, which is what a browser does with an unknown family. A generic name is resolved
    // to a system face on first use and cached under its own name.
    auto resolve = [](std::string const& name) -> TtfFont const* {
        auto& faces = face_registry();
        if (auto found = faces.find(name); found != faces.end())
            return found->second;
        char const* const path = system_font_path(name);
        if (!path)
            return nullptr;
        auto const data = read_file(path);
        if (data.empty())
            return nullptr;
        if (!register_font_face(name, data))
            return nullptr;
        auto found = face_registry().find(name);
        return found != face_registry().end() ? found->second : nullptr;
    };

    std::string name;
    for (char c : family) {
        if (c == ',') {
            if (auto const* face = resolve(trim_copy(name)))
                return face;
            name.clear();
            continue;
        }
        name += c;
    }
    return resolve(trim_copy(name));
}

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
    // Key on the rounded size so 16px and 16.2px share an entry, and on the font's identity so a glyph id from
    // one font is never served from another's bitmap. With a single active font the identity never varied, which
    // is why the key did not carry it; `@font-face` makes it vary (row 3d1).
    auto size_key = static_cast<int>(pixel_size * 4.0f + 0.5f);
    static std::map<std::tuple<uint64_t, int, int>, GlyphBitmap> cache;
    auto key = std::make_tuple(font.identity(), static_cast<int>(glyph), size_key);
    auto found = cache.find(key);
    if (found != cache.end())
        return found->second;
    auto inserted = cache.emplace(key, rasterize_glyph(font, glyph, static_cast<float>(size_key) / 4.0f));
    return inserted.first->second;
}

} // namespace aetheris::rendering

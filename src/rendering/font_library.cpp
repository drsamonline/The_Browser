#include "font_library.hpp"

#include "glyph_rasterizer.hpp"

#include <map>
#include <set>
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

// A generic family has no face of its own. On Linux the mechanism a browser uses is fontconfig, and upstream's
// SystemFontMatcher treats fontconfig as *the* mechanism for the generics: resolve_generic_family documents
// itself as "The installed family for a generic family, such as 'sans-serif'", and the DirectWrite backend
// records that DirectWrite "has no families for the generic families, unlike fontconfig" - which is why the
// hardcoded table there is the Windows exception rather than the rule.
//
// This engine links no fontconfig, so it names the files fontconfig resolves these generics to on a Debian
// desktop, in preference order, and falls back to the embedded face only if none of them loads.
//
// What it changes, measured at 14px on `A web browser, often abbreviated as browser, is an application`: the
// embedded DejaVu Sans advances 441.61px and Noto Sans 406.83px, so a line set in `sans-serif` held about 8%
// fewer words than the system face and broke early (row 0.22).
//
// What it deliberately does *not* match: `fc-match sans-serif` answers Noto Sans, and so does this table, but the
// reference browser does not follow fontconfig for the generics. It resolves `sans-serif` through its own default
// family name - Arial, substituted to Liberation Sans, 386.66px - and `serif` through Times New Roman, likewise
// Liberation Serif, 350.77px, while agreeing with fontconfig on `monospace`. That is the reference browser's own
// font stack, not fontconfig's answer, and following fontconfig is what a browser on Linux should do, so the
// residual difference is left standing rather than copied. See docs/HANDOVER.md 7q.
struct GenericFamily {
    char const* family;
    char const* candidates[3];
};

GenericFamily const* generic_family(std::string const& name)
{
    static GenericFamily const generics[] = {
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
        if (name == generic.family)
            return &generic;
    }
    return nullptr;
}

// A name that resolved to nothing is remembered as such. Without this, every text run re-reads and re-parses a
// half-megabyte font file to reach the same "no" - a per-run cost on a page that names a family the machine
// does not have. Only failures are cached here; a successful load lives in the face registry, which is checked
// first, so a face registered later still wins.
std::set<std::string>& unresolved_families()
{
    static std::set<std::string> unresolved;
    return unresolved;
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
        auto& unresolved = unresolved_families();
        if (unresolved.count(name))
            return nullptr;
        TtfFont const* face = nullptr;
        if (auto const* generic = generic_family(name)) {
            // Every candidate is tried, not only the first that exists. A file can be present and still fail to
            // parse, and the list is a fallback chain only if an unloadable candidate hands over to the next.
            for (auto const* path : generic->candidates) {
                if (!path)
                    break;
                auto const data = read_file(path);
                if (data.empty())
                    continue;
                if (!register_font_face(name, data))
                    continue;
                auto found = face_registry().find(name);
                if (found != face_registry().end()) {
                    face = found->second;
                    break;
                }
            }
        }
        if (!face)
            unresolved.insert(name);
        return face;
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

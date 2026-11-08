// Parses a real TrueType font: table directory, cmap, metrics and glyph
// outlines. Skips (passes) when no font is available, so the suite does not
// depend on a particular machine having fonts installed.

#include <ttf_font.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace aetheris::rendering;

static void require(bool condition, char const* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

static std::vector<uint8_t> read_file(std::string const& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

int main()
{
    std::vector<std::string> candidates;
    if (auto* from_env = std::getenv("AETHERIS_TEST_FONT"))
        candidates.push_back(from_env);
    candidates.push_back("/usr/local/lib/python3.12/site-packages/matplotlib/mpl-data/fonts/ttf/DejaVuSans.ttf");
    candidates.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    candidates.push_back("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");

    std::vector<uint8_t> data;
    std::string used;
    for (auto const& path : candidates) {
        data = read_file(path);
        if (!data.empty()) {
            used = path;
            break;
        }
    }
    if (data.empty()) {
        std::printf("no font found, skipping\n");
        return 0;
    }

    TtfFont font;
    require(font.load(data), "font loads");
    require(font.units_per_em() > 0, "unitsPerEm is set");
    require(font.glyph_count() > 100, "the font has a realistic glyph count");
    require(font.ascender() > 0, "ascender is positive");
    require(font.descender() < 0, "descender is negative");

    // cmap: 'A' must map to a real glyph with an advance.
    auto a = font.glyph_index('A');
    require(a != 0, "'A' maps to a glyph");
    require(font.advance_width(a) > 0, "'A' has an advance width");

    // A different character must map to a different glyph.
    auto b = font.glyph_index('B');
    require(b != 0 && b != a, "'B' maps to its own glyph");

    // Space is mapped but has no outline.
    auto space = font.glyph_index(' ');
    require(space != 0, "space maps to a glyph");
    require(font.outline(space).empty(), "space has no outline");

    // 'A' is a filled shape: at least one contour with points, in font units.
    auto outline = font.outline(a);
    require(!outline.empty(), "'A' has an outline");
    std::size_t points = 0;
    float max_x = 0;
    for (auto const& contour : outline.contours) {
        points += contour.points.size();
        for (auto const& point : contour.points)
            if (point.x > max_x)
                max_x = point.x;
    }
    require(points >= 3, "'A' outline has points");
    require(max_x > 0 && max_x <= static_cast<float>(font.units_per_em()), "points are in font units");

    // An unmapped codepoint resolves to .notdef rather than a random glyph.
    require(font.glyph_index(0x10FFFF) == 0, "unmapped codepoint gives .notdef");

    std::printf("parsed %s: %u glyphs, %u units/em, %zu points in 'A'\n",
        used.c_str(), font.glyph_count(), font.units_per_em(), points);
    return 0;
}

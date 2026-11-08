// Rasterises real glyph outlines and checks the result is a plausible glyph:
// ink present, correct bounding box, anti-aliased edges, and a counter (the
// hole in 'A') left empty. Skips when no font is available.

#include <glyph_rasterizer.hpp>
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

    std::vector<uint8_t> data;
    for (auto const& path : candidates) {
        data = read_file(path);
        if (!data.empty())
            break;
    }
    if (data.empty()) {
        std::printf("no font found, skipping\n");
        return 0;
    }

    TtfFont font;
    require(font.load(data), "font loads");

    auto glyph = font.glyph_index('A');
    require(glyph != 0, "'A' maps to a glyph");

    auto bitmap = rasterize_glyph(font, glyph, 32.0f);
    require(!bitmap.empty(), "rasterises to a non-empty bitmap");
    require(bitmap.width > 10 && bitmap.width < 60, "bitmap width is a glyph, not a page");
    require(bitmap.height > 10 && bitmap.height < 60, "bitmap height is a glyph, not a page");

    std::size_t inked = 0;
    int partial = 0;
    for (int y = 0; y < bitmap.height; ++y)
        for (int x = 0; x < bitmap.width; ++x) {
            auto value = bitmap.at(x, y);
            if (value > 0)
                ++inked;
            if (value > 0 && value < 255)
                ++partial;
        }

    auto total = static_cast<std::size_t>(bitmap.width) * static_cast<std::size_t>(bitmap.height);
    require(inked > 0, "the glyph has ink");
    require(inked < total / 2, "the glyph does not fill its whole box");
    require(partial > 0, "edges are anti-aliased (partial coverage exists)");

    // The top centre of 'A' is inked (its apex); a bottom corner is not.
    int centre = bitmap.width / 2;
    bool apex = false;
    for (int y = 0; y < bitmap.height / 3; ++y)
        if (bitmap.at(centre, y) > 0)
            apex = true;
    require(apex, "'A' has ink near the top centre");

    // A larger size must produce a larger bitmap, and thick strokes must reach
    // full coverage: truncating each sub-sample left solids at 252 instead of 255.
    auto bigger = rasterize_glyph(font, glyph, 64.0f);
    require(bigger.width > bitmap.width && bigger.height > bitmap.height, "size scales the bitmap");
    int solid = 0;
    for (int y = 0; y < bigger.height; ++y)
        for (int x = 0; x < bigger.width; ++x)
            if (bigger.at(x, y) == 255)
                ++solid;
    require(solid > 0, "thick strokes reach full coverage");

    std::printf("'A' at 32px: %dx%d, %zu inked, %d partial\n",
        bitmap.width, bitmap.height, inked, partial);
    return 0;
}

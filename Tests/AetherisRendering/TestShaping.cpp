// Does the engine shape, and does it report positions a layout can use?
//
// The engine's own embedded font is the fixture: a DejaVu Sans subset, so a real TTF with real
// GSUB/GPOS tables. The strong ligature proof lives in tools/run-shaping-smoke.sh, where the font is
// controlled; what this test pins is the contract the text path will rely on.
#include "embedded_font.inc"
#include <shaper.hpp>
#include <ttf_font.hpp>

#include <cstdlib>
#include <iostream>
#include <vector>

using namespace aetheris::rendering;

static void require(bool condition, char const* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    require(shaping_available(), "this build has a shaper (HarfBuzz was found at configure time)");

    TtfFont font;
    std::vector<uint8_t> data;
    // The same bytes the engine renders with, so a font the engine cannot read cannot pass this test.
    data.assign(kEmbeddedFontData, kEmbeddedFontData + sizeof(kEmbeddedFontData));
    require(font.load(data), "the embedded font loads");
    require(!font.data().empty(), "the font exposes its bytes to the shaper");

    // Latin text: one glyph per character, clusters in order, advances positive.
    auto latin = shape_run(font.data(), "abc", 16.0f);
    require(latin.size() == 3, "three characters shape to three glyphs");
    require(latin[0].cluster == 0 && latin[1].cluster == 1 && latin[2].cluster == 2,
        "clusters are byte offsets in order");
    for (auto const& glyph : latin)
        require(glyph.advance > 0, "every glyph advances");

    // "fi" is the case the engine got wrong: one code point pair, one or two glyphs, never three.
    auto fi = shape_run(font.data(), "fi", 16.0f);
    require(!fi.empty() && fi.size() <= 2, "\"fi\" shapes to one or two glyphs");
    require(fi.front().cluster == 0, "the first glyph starts at byte 0");
    float const fi_advance = [&] { float total = 0; for (auto const& g : fi) total += g.advance; return total; }();
    require(fi_advance > 0, "the run has width");

    // An empty run is empty, not a crash - text nodes are often empty.
    require(shape_run(font.data(), "", 16.0f).empty(), "empty text shapes to nothing");

    // Byte offsets, not code point indices: "é" is two bytes, so the cluster after it is 2.
    auto accented = shape_run(font.data(), "\xc3\xa9z", 16.0f);
    require(accented.size() == 2, "an accented character shapes to one glyph plus one");
    require(accented[1].cluster == 2, "the second cluster is a byte offset, not a code point index");

    std::cout << "shaping: " << latin.size() << " latin glyphs, " << fi.size() << " for \"fi\", clusters are byte offsets\n";
    return 0;
}

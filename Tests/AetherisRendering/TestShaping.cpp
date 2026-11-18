// Does the engine shape, and does it report positions a layout can use?
//
// The engine's own embedded font is the fixture: a DejaVu Sans subset, so a real TTF with real
// GSUB/GPOS tables. The strong ligature proof lives in tools/run-shaping-smoke.sh, where the font is
// controlled; what this test pins is the contract the text path will rely on.
#include "embedded_font.inc"
#include <font.hpp>
#include <font_library.hpp>
#include <shaper.hpp>
#include <ttf_font.hpp>

#include <cstdlib>
#include <fstream>
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

    // The point of wiring the shaper in: the engine's own width must reflect the ligature. If
    // Font::measure still summed code points, "fi" would measure as wide as "f" plus "i" and the
    // engine would lay out text with gaps where a ligature collapsed two characters into one glyph.
    // The engine only shapes when it has an outline font loaded, which a unit test has to ask for:
    // `active_font()` reads AETHERIS_FONT or a list of system paths, and returns null otherwise - in
    // which case `measure` falls back to the bitmap font and this assertion is meaningless.
    {
        // `active_font()` loads once from AETHERIS_FONT or a system path list, so the test writes the
        // embedded font to a file and points the variable at it before the first call.
        std::string const path = "/tmp/aetheris-shaping-test.ttf";
        {
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<char const*>(data.data()), static_cast<std::streamsize>(data.size()));
        }
        ::setenv("AETHERIS_FONT", path.c_str(), 1);
        require(active_font() != nullptr, "an outline font is loaded for this test");
        Font const font_style = Font::from_style([] { StyleProperties p; p.set("font-size", "16px"); return p; }());
        float const fi = font_style.measure("fi");
        auto const fi_glyphs = shape_run(font.data(), "fi", 16.0f).size();
        float const f_plus_i = font_style.measure("f") + font_style.measure("i");
        // Strictly less, by however little: in this font the ligature advance is 10.0781 against
        // 10.0938 for the two characters, so the difference is 0.0157 px and a threshold of half a
        // pixel would fail on a working shaper. The assertion is that the two disagree, and that the
        // shaped one is the smaller - the size of the gap belongs to the font, not to the test.
        require(fi < f_plus_i, "Font::measure reflects the ligature: shaped \"fi\" is narrower than \"f\" + \"i\"");
        require(fi_glyphs == 1, "and it is narrower because the two characters became one glyph");
    }

    std::cout << "shaping: " << latin.size() << " latin glyphs, " << fi.size() << " for \"fi\", clusters are byte offsets\n";
    return 0;
}

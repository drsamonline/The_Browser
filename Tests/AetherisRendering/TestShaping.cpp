// Does the engine shape, and does it report positions a layout can use?
//
// The engine's own embedded font is the fixture: a DejaVu Sans subset, so a real TTF with real
// GSUB/GPOS tables. The strong ligature proof lives in tools/run-shaping-smoke.sh, where the font is
// controlled; what this test pins is the contract the text path will rely on.
#include "embedded_font.inc"
#include <font.hpp>
#include <font_library.hpp>
#include <shaper.hpp>
#include <text_layout.hpp>
#include <ttf_font.hpp>

#include <cmath>
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

    // The shaper's cache key. A content hash, so two loads of the same file share one face - and
    // computed at load, not per run: hashing the file on every run was 98% of the cost of a shaped run
    // (row 2d4, tools/shaping-bench.cpp).
    require(font.identity() != 0, "a loaded font has an identity");
    {
        TtfFont second;
        require(second.load(data), "the same bytes load again");
        require(second.identity() == font.identity(), "identity is the file's content, so a reload shares the face");
    }

    // Latin text: one glyph per character, clusters in order, advances positive.
    auto latin = shape_run(font.data(), "abc", 16.0f, font.identity());
    require(latin.size() == 3, "three characters shape to three glyphs");
    require(latin[0].cluster == 0 && latin[1].cluster == 1 && latin[2].cluster == 2,
        "clusters are byte offsets in order");
    for (auto const& glyph : latin)
        require(glyph.advance > 0, "every glyph advances");

    // "fi" is the case the engine got wrong: one code point pair, one or two glyphs, never three.
    auto fi = shape_run(font.data(), "fi", 16.0f, font.identity());
    require(!fi.empty() && fi.size() <= 2, "\"fi\" shapes to one or two glyphs");
    require(fi.front().cluster == 0, "the first glyph starts at byte 0");
    float const fi_advance = [&] { float total = 0; for (auto const& g : fi) total += g.advance; return total; }();
    require(fi_advance > 0, "the run has width");

    // An empty run is empty, not a crash - text nodes are often empty.
    require(shape_run(font.data(), "", 16.0f, font.identity()).empty(), "empty text shapes to nothing");

    // Byte offsets, not code point indices: "é" is two bytes, so the cluster after it is 2.
    auto accented = shape_run(font.data(), "\xc3\xa9z", 16.0f, font.identity());
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
        auto const fi_glyphs = shape_run(font.data(), "fi", 16.0f, font.identity()).size();
        float const f_plus_i = font_style.measure("f") + font_style.measure("i");
        // Strictly less, by however little: in this font the ligature advance is 10.0781 against
        // 10.0938 for the two characters, so the difference is 0.0157 px and a threshold of half a
        // pixel would fail on a working shaper. The assertion is that the two disagree, and that the
        // shaped one is the smaller - the size of the gap belongs to the font, not to the test.
        require(fi < f_plus_i, "Font::measure reflects the ligature: shaped \"fi\" is narrower than \"f\" + \"i\"");
        require(fi_glyphs == 1, "and it is narrower because the two characters became one glyph");

        // Row 2h: layout must measure what the pen draws. measure_text() used to sum code point advances
        // and skip kerning entirely, so a fragment's box was wider than its glyphs and the next fragment
        // started where the text was not. "To" is the sharpest case in this font: 19.5625 px summed
        // against 16.8594 px shaped at 16 px.
        StyleProperties layout_style;
        layout_style.set("font-size", "16px");
        float const layout_to = TextLayout::measure_text("To", 16.0f, layout_style);
        float const shaped_to = font_style.measure("To");
        require(std::abs(layout_to - shaped_to) < 0.01f, "layout measures the shaped width of a kerned pair");
        float const layout_t_plus_o = TextLayout::measure_text("T", 16.0f, layout_style) + TextLayout::measure_text("o", 16.0f, layout_style);
        require(layout_to < layout_t_plus_o - 0.5f, "and that width carries the kerning, not just the advances");

    }

    // The shaped-run cache must be invisible. The same call twice returns the same glyphs, and the size
    // is in the key so a 32 px run is never served a 16 px one. Both are silent layout corruption if the
    // key is wrong, which is why they are pinned rather than assumed.
    {
        auto const first = shape_run(font.data(), "cache contract", 16.0f, font.identity());
        auto const second = shape_run(font.data(), "cache contract", 16.0f, font.identity());
        require(!first.empty() && first.size() == second.size(), "a cached run has the same length as the first one");
        bool same = true;
        for (std::size_t i = 0; i < first.size() && same; ++i)
            same = first[i].glyph == second[i].glyph && first[i].cluster == second[i].cluster
                && first[i].advance == second[i].advance;
        require(same, "a cached run has the same glyphs, clusters and advances as the first one");

        auto const doubled = shape_run(font.data(), "cache contract", 32.0f, font.identity());
        require(doubled.size() == first.size(), "the same text at 32 px shapes to the same glyph count");
        float wide = 0;
        float narrow = 0;
        for (auto const& glyph : doubled)
            wide += glyph.advance;
        for (auto const& glyph : first)
            narrow += glyph.advance;
        require(wide > narrow * 1.5f, "the size is in the cache key: 32 px advances are not the 16 px ones");
    }

    std::cout << "shaping: " << latin.size() << " latin glyphs, " << fi.size() << " for \"fi\", clusters are byte offsets\n";
    return 0;
}

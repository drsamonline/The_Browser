// Does the engine shape, and does it report positions a layout can use?
//
// The engine's own embedded font is the fixture: a DejaVu Sans subset, so a real TTF with real
// GSUB/GPOS tables. The strong ligature proof lives in tools/run-shaping-smoke.sh, where the font is
// controlled; what this test pins is the contract the text path will rely on.
#include "embedded_font.inc"
#include <font.hpp>
#include <font_library.hpp>
#include <render_document.hpp>
#include <render_tree.hpp>
#include <shaper.hpp>
#include <text_layout.hpp>
#include <ttf_font.hpp>

#include <algorithm>
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

static std::string base64_of(std::vector<uint8_t> const& bytes)
{
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    while (i + 2 < bytes.size()) {
        unsigned const chunk = (static_cast<unsigned>(bytes[i]) << 16)
            | (static_cast<unsigned>(bytes[i + 1]) << 8) | static_cast<unsigned>(bytes[i + 2]);
        out.push_back(alphabet[(chunk >> 18) & 0x3f]);
        out.push_back(alphabet[(chunk >> 12) & 0x3f]);
        out.push_back(alphabet[(chunk >> 6) & 0x3f]);
        out.push_back(alphabet[chunk & 0x3f]);
        i += 3;
    }
    auto const rest = bytes.size() - i;
    if (rest == 1) {
        unsigned const chunk = static_cast<unsigned>(bytes[i]) << 16;
        out.push_back(alphabet[(chunk >> 18) & 0x3f]);
        out.push_back(alphabet[(chunk >> 12) & 0x3f]);
        out += "==";
    } else if (rest == 2) {
        unsigned const chunk = (static_cast<unsigned>(bytes[i]) << 16) | (static_cast<unsigned>(bytes[i + 1]) << 8);
        out.push_back(alphabet[(chunk >> 18) & 0x3f]);
        out.push_back(alphabet[(chunk >> 12) & 0x3f]);
        out.push_back(alphabet[(chunk >> 6) & 0x3f]);
        out.push_back('=');
    }
    return out;
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

// Cluster-safe breaking (row 2f). A break offset is a byte position a line may break at without
    // changing what is drawn; the end of the text is always one.
    {
        auto const has = [](std::vector<std::size_t> const& offsets, std::size_t value) {
            return std::find(offsets.begin(), offsets.end(), value) != offsets.end();
        };

        // "office" is 6 bytes. The ffi ligature means the clusters are 0, 1, 4, 5 - so a break after "of"
        // is at byte 4, and bytes 2 and 3 are inside the ligature and never offered.
        auto const office = safe_break_offsets(font.data(), "office", 16.0f, font.identity());
        require(!has(office, 2) && !has(office, 3), "no break inside the ffi ligature");
        require(has(office, 4), "a break after \"of\" is offered");
        require(!office.empty() && office.back() == 6, "the end of the text is the last offset");

        // A kerned pair: breaking before "o" would change the spacing, so only the end is offered.
        auto const to = safe_break_offsets(font.data(), "To", 16.0f, font.identity());
        require(to.size() == 1 && to.front() == 2, "a kerned pair is not split");

        // A combining mark shares its base's cluster, so byte 1 is inside a cluster and never offered.
        auto const combining = safe_break_offsets(font.data(), "e\xcc\x81x", 16.0f, font.identity());
        require(!has(combining, 1), "no break between a letter and its combining mark");

        // The wiring: a word that cannot fit is broken only when the page asks, and only at those offsets.
        StyleProperties overflow_style;
        overflow_style.set("font-size", "16px");
        auto const overflowing = TextLayout::layout("officeoffice", 0, 0, 40, overflow_style);
        require(overflowing.size() == 1, "by default a long word overflows rather than breaking, as a browser does");

        StyleProperties break_style;
        break_style.set("font-size", "16px");
        break_style.set("overflow-wrap", "break-word");
        auto const broken = TextLayout::layout("officeoffice", 0, 0, 40, break_style);
        require(broken.size() > 1, "overflow-wrap: break-word breaks it instead");
        std::size_t rejoined = 0;
        for (auto const& fragment : broken) {
            require(TextLayout::measure_text(fragment.text, 16.0f, break_style) <= 40.0f + 0.01f,
                "every piece fits the line");
            rejoined += fragment.text.size();
        }
        require(rejoined == 12, "the pieces are the word, all of it and nothing else");
    }

// Spacing reaches the advances, and the cache key separates spaced from unspaced (row 2g). The bitmap
    // fallback makes the arithmetic exact: 10 px per character at 16 px, so "abc" is 30 px.
    {
        StyleProperties plain;
        plain.set("font-size", "16px");
        StyleProperties spaced;
        spaced.set("font-size", "16px");
        spaced.set("letter-spacing", "2px");
        StyleProperties wide;
        wide.set("font-size", "16px");
        wide.set("word-spacing", "5px");

        float const plain_abc = TextLayout::measure_text("abc", 16.0f, plain);
        float const spaced_abc = TextLayout::measure_text("abc", 16.0f, spaced);
        require(std::abs((spaced_abc - plain_abc) - 6.0f) < 0.01f, "letter-spacing adds once per glyph: 3 glyphs x 2 px");

        float const plain_pair = TextLayout::measure_text("a b", 16.0f, plain);
        float const wide_pair = TextLayout::measure_text("a b", 16.0f, wide);
        require(std::abs((wide_pair - plain_pair) - 5.0f) < 0.01f, "word-spacing adds once, at the space");

        // The cache must not serve a spaced run to an unspaced request: ask for the spaced width first, then
        // the plain one, and both must still be right.
        require(std::abs(TextLayout::measure_text("abc", 16.0f, plain) - plain_abc) < 0.01f,
            "a run shaped with spacing is not served to a request without it");
        require(std::abs(TextLayout::measure_text("abc", 16.0f, spaced) - spaced_abc) < 0.01f,
            "and the other way round");

        // Layout and paint must agree: the fragment's box is the width the pen will advance by.
        auto const fragments = TextLayout::layout("abc", 0, 0, 200, spaced);
        require(fragments.size() == 1, "one word, one fragment");
        require(std::abs(fragments[0].rect.width - spaced_abc) < 0.01f, "the fragment is as wide as the spaced run");
    }

    // Row 2c: kerning, in both paths. A shaped run gets it from GPOS (asserted above: the shaped width of
    // "To" is narrower than the two characters), and a build without a shaper gets it from the embedded pair
    // table. That table holds the embedded subset's glyph ids, so it answers only for the active font:
    // asking the local TtfFont returns 0 for every pair, which is how the first version of this failed.
    {
        auto const* active = active_font();
        require(active != nullptr, "an outline font is active for the pair table");
        int const kern = kerning_adjustment(*active, active->glyph_index('A'), active->glyph_index('T'));
        require(kern != 0, "the embedded pair table has a value for A + T");

        StyleProperties kern_style;
        kern_style.set("font-size", "16px");
        auto const kern_font = Font::from_style(kern_style);
        float const shaped_pair = kern_font.measure("AT");
        float const apart = kern_font.measure("A") + kern_font.measure("T");
        require(shaped_pair < apart, "and GPOS kerns the same pair in the shaped path");
    }

// Row 3d1: a face declared by `@font-face` is registered, `font-family` selects it, and the pen draws with
    // it. The fixture writes the embedded font to /tmp and declares it, so the whole path runs without a network.
    {
        require(register_font_face("AetherisTestFace", data), "the embedded bytes register as a face");
        require(font_for_family("AetherisTestFace") != nullptr, "and the family finds it");
        require(font_for_family("NoSuchFace") == nullptr, "an unknown family has no face");
        require(font_for_family("NoSuchFace, AetherisTestFace") != nullptr, "a family list falls through to the one that exists");

        StyleProperties named;
        named.set("font-size", "16px");
        named.set("font-family", "AetherisTestFace");
        auto const named_font = Font::from_style(named);
        require(named_font.face() == font_for_family("AetherisTestFace"), "Font::from_style carries the named face");

        StyleProperties unnamed;
        unnamed.set("font-size", "16px");
        unnamed.set("font-family", "NoSuchFace");
        require(Font::from_style(unnamed).face() == active_font(), "an unknown family falls back to the engine's font");

        // End to end: the stylesheet's `@font-face` is read from disk and registered when the document is built,
        // and the text command carries the family so paint can resolve the same face.
        std::string const path = "/tmp/aetheris-fontface-test.ttf";
        {
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<char const*>(data.data()), static_cast<std::streamsize>(data.size()));
        }
        auto document = RenderDocument::create("<p>text</p>",
            "@font-face { font-family: 'FromCss'; src: url(" + path + "); } p { font-family: FromCss; }", 200);
        require(font_for_family("FromCss") != nullptr, "building the document registered the declared face");
        bool carried = false;
        for (auto const& command : document.render_tree().commands())
            if (command.type == PaintCommand::Type::DrawText && command.font_family == "FromCss")
                carried = true;
        require(carried, "and the text command carries the family for paint to resolve");
        std::remove(path.c_str());
    }

    std::cout << "shaping: " << latin.size() << " latin glyphs, " << fi.size() << " for \"fi\", clusters are byte offsets\n";
    // Row 3d1's tail: a `data:` URI carries the font inline, so a page can ship a face without a file
    // or a network. Base64 is decoded; anything else in a `data:` URI is skipped rather than guessed at.
    {
        auto const encoded = base64_of(data);
        auto inline_document = RenderDocument::create("<p>text</p>",
            "@font-face { font-family: 'InlineFace'; src: url(data:font/ttf;base64," + encoded + "); }"
            "p { font-family: InlineFace; }",
            200);
        require(font_for_family("InlineFace") != nullptr, "a base64 data: URI registers the face");
        bool carried = false;
        for (auto const& command : inline_document.render_tree().commands())
            if (command.type == PaintCommand::Type::DrawText && command.font_family == "InlineFace")
                carried = true;
        require(carried, "and paint is told the family");

        // A `data:` URI that is not base64 is not a font this engine can read, so the family stays absent
        // rather than silently resolving to something wrong.
        RenderDocument::create("<p>text</p>",
            "@font-face { font-family: 'PercentFace'; src: url(data:font/ttf,%00%01%02); }"
            "p { font-family: PercentFace; }",
            200);
        require(font_for_family("PercentFace") == nullptr, "a percent-encoded data: URI is skipped");

        // A remote face is registered from the resource cache: the host fetches `@font-face` sources, because
        // they are named inside the stylesheet and never appear in the document's dependency list. This is
        // the lookup half of that path, without a network.
        ResourceCache cache;
        cache.put({ ResourceType::Font, "https://fonts.test/body.ttf", std::vector<uint8_t>(data.begin(), data.end()) });
        RenderDocument::create("<p>text</p>",
            "@font-face { font-family: 'RemoteFace'; src: url(https://fonts.test/body.ttf); }"
            "p { font-family: RemoteFace; }",
            200, &cache);
        require(font_for_family("RemoteFace") != nullptr, "a face whose bytes are cached registers");

        // The same rule with nothing cached must not invent a face.
        RenderDocument::create("<p>text</p>",
            "@font-face { font-family: 'UncachedFace'; src: url(https://fonts.test/missing.ttf); }"
            "p { font-family: UncachedFace; }",
            200);
        require(font_for_family("UncachedFace") == nullptr, "an uncached remote face is skipped, not guessed");
    }

    return 0;
}

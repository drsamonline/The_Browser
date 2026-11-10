// The binary must render text without relying on the machine having fonts, so a
// DejaVu Sans subset is embedded. This checks it parses and covers Latin.

#include <font.hpp>
#include <font_library.hpp>
#include <ttf_font.hpp>

#include <cstdio>
#include <cmath>
#include <cstdlib>

using namespace aetheris::rendering;

static void require(bool condition, char const* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

int main()
{
    // Unset any override so the embedded font is the one under test.
#if defined(_WIN32)
    _putenv_s("AETHERIS_FONT", "");   // no unsetenv on Windows
#else
    ::unsetenv("AETHERIS_FONT");
#endif

    TtfFont const* font = active_font();
    require(font != nullptr, "an embedded font is available without any system font");
    require(font->loaded(), "the embedded font parses");
    require(font->units_per_em() > 0, "unitsPerEm is set");
    require(font->glyph_count() > 100, "the subset keeps a realistic glyph count");

    // Basic Latin and Latin-1 must both be covered.
    for (char c = 'A'; c <= 'Z'; ++c)
        require(font->glyph_index(static_cast<unsigned char>(c)) != 0, "uppercase Latin is mapped");
    for (char c = 'a'; c <= 'z'; ++c)
        require(font->glyph_index(static_cast<unsigned char>(c)) != 0, "lowercase Latin is mapped");
    require(font->glyph_index(0x00E9) != 0, "e-acute is mapped (Latin-1)");

    // And a glyph really has an outline to draw.
    require(!font->outline(font->glyph_index('A')).empty(), "'A' has an outline");

    // Kerning: "AV" must be narrower than "A" plus "V" on their own.
    {
        StyleProperties style;
        style.set("font-size", "32");
        Font measured = Font::from_style(style);
        float av = measured.measure("AV");
        float separate = measured.measure("A") + measured.measure("V");
        require(av < separate - 0.5f, "kerning tightens 'AV'");
        // A pair with no kerning must be unaffected.
        float xy = measured.measure("xy");
        float xy_separate = measured.measure("x") + measured.measure("y");
        require(std::abs(xy - xy_separate) < 0.5f, "unrelated pairs are not kerned");
    }

    std::printf("embedded font: %u glyphs, %u units/em\n", font->glyph_count(), font->units_per_em());
    return 0;
}

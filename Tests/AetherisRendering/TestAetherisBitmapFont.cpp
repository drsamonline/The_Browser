#include "bitmap_font.hpp"
#include "software_surface.hpp"

#include <cstdio>
#include <cstdlib>

using namespace aetheris::rendering;

static void require(bool condition, char const* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

static int marked_pixels(SoftwareSurface const& surface)
{
    int marked = 0;
    for (int y = 0; y < surface.height(); ++y)
        for (int x = 0; x < surface.width(); ++x) {
            auto pixel = surface.pixel(x, y);
            if (pixel.red || pixel.green || pixel.blue)
                ++marked;
        }
    return marked;
}

int main()
{
    // Glyph lookup: 'A' has the expected bitmap rows.
    auto const& letter_a = bitmap_glyph_for('A');
    require(letter_a.rows[0] == 0x0C && letter_a.rows[4] == 0x3F, "glyph data for 'A'");

    // Anything outside printable ASCII falls back to the '?' glyph.
    auto const& fallback = bitmap_glyph_for('\x01');
    auto const& question = bitmap_glyph_for('?');
    for (int row = 0; row < 8; ++row)
        require(fallback.rows[row] == question.rows[row], "non-ASCII falls back to '?'");

    // Metrics grow with the font size and stay positive.
    require(bitmap_advance(16) > bitmap_advance(8), "advance scales with size");
    require(bitmap_advance(8) >= 1.0f, "advance is positive");
    require(bitmap_cell_height(16) == 16.0f, "cell height tracks size");

    // Rasterising a glyph marks pixels.
    SoftwareSurface surface(64, 32, Color { 0, 0, 0, 0 });
    surface.draw_text({ 0, 0, 64, 16 }, "A", 16, Color { 255, 255, 255, 255 });
    require(marked_pixels(surface) > 0, "glyph rasterises pixels");

    // A space draws nothing.
    SoftwareSurface blank(16, 16, Color { 0, 0, 0, 0 });
    blank.draw_text({ 0, 0, 16, 16 }, " ", 16, Color { 255, 255, 255, 255 });
    require(marked_pixels(blank) == 0, "space draws nothing");

    // Different characters produce different rasterisations.
    SoftwareSurface i_surface(64, 32, Color { 0, 0, 0, 0 });
    i_surface.draw_text({ 0, 0, 64, 16 }, "i", 16, Color { 255, 255, 255, 255 });
    require(marked_pixels(i_surface) > 0 && marked_pixels(i_surface) < marked_pixels(surface), "distinct glyphs differ");

    return 0;
}

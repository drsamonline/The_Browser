#include <color.hpp>
#include <render_document.hpp>
#include <software_surface.hpp>

#include <cstdlib>
#include <iostream>

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
    auto short_hex = Color::parse("#f0a");
    require(short_hex && short_hex->red == 255 && short_hex->green == 0 && short_hex->blue == 170, "short hex color");

    auto rgba_hex = Color::parse("#10203040");
    require(rgba_hex && rgba_hex->alpha == 64, "rgba hex color");

    SoftwareSurface clipped(4, 4);
    clipped.fill_rect({ -2, -2, 4, 4 }, { 255, 0, 0, 255 });
    require(clipped.pixel(0, 0).red == 255, "clipped fill enters surface");
    require(clipped.pixel(3, 3).red == 0, "clipped fill does not overrun");

    auto rendered = RenderDocument::create(
        "<div>Hello renderer</div>",
        "div { width: 40px; background-color: #336699; color: white; font-size: 10px; }",
        80);

    auto surface = rendered.render_to_surface(80, 40);
    bool found_background = false;
    bool found_text = false;
    for (int y = 0; y < surface.height(); ++y) {
        for (int x = 0; x < surface.width(); ++x) {
            auto pixel = surface.pixel(x, y);
            found_background = found_background || (pixel.red == 0x33 && pixel.green == 0x66 && pixel.blue == 0x99);
            found_text = found_text || (pixel.red == 255 && pixel.green == 255 && pixel.blue == 255);
        }
    }

    require(found_background, "background paint command executed");
    require(found_text, "text paint command executed");
    return 0;
}

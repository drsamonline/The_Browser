#include <image_output.hpp>
#include <render_document.hpp>

#include <cstdlib>
#include <filesystem>
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
    auto rendered = RenderDocument::create(
        "<div><span>clip me</span></div>",
        "div { width: 30px; height: 20px; background-color: red; border-width: 2px; border-color: blue;"
        "border-radius: 8px; border-style: dashed; overflow: hidden; opacity: 0.5; }"
        "span { color: white; font-size: 18px; white-space: nowrap; }",
        80);

    bool rounded = false;
    bool stroked = false;
    for (auto const& command : rendered.render_tree().commands()) {
        rounded = rounded || command.type == PaintCommand::Type::FillRoundedRect;
        stroked = stroked || command.type == PaintCommand::Type::StrokeRoundedRect;
    }
    require(rounded, "rounded background command");
    require(stroked, "rounded border command");

    auto surface = rendered.render_to_surface(80, 40);
    auto corner = surface.pixel(0, 0);

    // The rounded corner lies outside the div, so the background shows through
    // there while the div body is painted. Text is now real glyphs (not a solid
    // cell), so scan for the composited white-text pixels rather than sampling a
    // single fixed point.
    bool body_painted = false;
    bool text_composited = false;
    for (int y = 0; y < surface.height(); ++y) {
        for (int x = 0; x < surface.width(); ++x) {
            auto pixel = surface.pixel(x, y);
            if (pixel.red > corner.red)
                body_painted = true;
            if (pixel.red > 100 && pixel.green > 0 && pixel.blue > 0)
                text_composited = true;
        }
    }
    require(body_painted, "rounded corner leaves background visible");
    require(text_composited, "opacity compositing applied");

    auto path = std::filesystem::temp_directory_path() / "aetheris_batch09.ppm";
    require(ImageOutput::write_ppm(surface, path.string()), "image output");
    std::filesystem::remove(path);
    return 0;
}

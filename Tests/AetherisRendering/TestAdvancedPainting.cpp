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
    auto center = surface.pixel(15, 10);
    require(center.red > corner.red, "rounded corner leaves background visible");
    require(center.red > 100 && center.blue > 0, "opacity compositing applied");

    auto path = std::filesystem::temp_directory_path() / "aetheris_batch09.ppm";
    require(ImageOutput::write_ppm(surface, path.string()), "image output");
    std::filesystem::remove(path);
    return 0;
}

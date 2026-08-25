#include <color.hpp>
#include <image_output.hpp>
#include <paint_executor.hpp>
#include <render_document.hpp>
#include <software_surface.hpp>

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
    auto blended = Color::blend_over({ 255, 0, 0, 128 }, { 0, 0, 255, 255 });
    require(blended.red > 100 && blended.blue > 100 && blended.alpha == 255, "alpha blend");

    SoftwareSurface surface(10, 10, { 255, 255, 255, 255 });
    surface.stroke_rect({ 1, 1, 8, 8 }, { 1, 2, 1, 2 }, { 0, 0, 0, 255 });
    require(surface.pixel(1, 1).red == 0, "border top");
    require(surface.pixel(5, 5).red == 255, "border interior");

    auto rendered = RenderDocument::create(
        "<div><span>inside</span></div>",
        "div { width: 30px; height: 12px; overflow: hidden; background-color: #336699; border-width: 2px; border-color: red; }"
        "span { color: #ffffff; font-size: 10px; }",
        60);
    auto output = rendered.render_to_surface(60, 30);
    require(output.pixel(0, 0).red == 255 || output.pixel(1, 1).red == 255, "border rendered");

    auto path = std::filesystem::temp_directory_path() / "aetheris_rendering_test.ppm";
    require(ImageOutput::write_ppm(output, path.string()), "ppm output");
    require(std::filesystem::file_size(path) > 16, "ppm contains pixels");
    std::filesystem::remove(path);
    return 0;
}

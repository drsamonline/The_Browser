#include "svg.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

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
    std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"32\" height=\"32\" viewBox=\"0 0 32 32\">"
        "<rect x=\"0\" y=\"0\" width=\"32\" height=\"32\" fill=\"#ffffff\"/>"
        "<rect x=\"4\" y=\"4\" width=\"10\" height=\"10\" fill=\"#ff0000\"/>"
        "<circle cx=\"24\" cy=\"24\" r=\"6\" fill=\"#0000ff\"/>"
        "<path d=\"M 2 26 L 10 26 L 6 20 Z\" fill=\"rgb(0,128,0)\"/>"
        "</svg>";
    std::vector<unsigned char> bytes(svg.begin(), svg.end());

    require(is_svg(bytes), "detect SVG");
    Image image;
    require(render_svg(bytes, image), "render SVG");
    require(image.width == 32 && image.height == 32, "SVG viewport size");

    auto red = image.pixel(8, 8);
    require(red.red > 200 && red.green < 60 && red.blue < 60, "red rect");

    auto blue = image.pixel(24, 24);
    require(blue.blue > 200 && blue.red < 60 && blue.green < 60, "blue circle");

    auto background = image.pixel(18, 8);
    require(background.red > 200 && background.green > 200 && background.blue > 200, "white background");

    auto triangle = image.pixel(6, 24);
    require(triangle.green > 90 && triangle.green < 170 && triangle.red < 60, "green triangle path");

    std::vector<unsigned char> junk { 'x' };
    require(!render_svg(junk, image), "reject non-SVG");

    return 0;
}

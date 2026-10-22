#include "gif.hpp"
#include "gif_test_data.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace aetheris::rendering;

static void require(bool condition, char const* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

static std::vector<unsigned char> bytes(unsigned char const* data, unsigned int length)
{
    return std::vector<unsigned char>(data, data + length);
}

static bool equals(Color const& color, int r, int g, int b)
{
    return color.red == r && color.green == g && color.blue == b;
}

int main()
{
    Image image;
    require(decode_gif(bytes(kGif, kGif_len), image), "decode GIF");
    require(image.width == 4 && image.height == 4, "GIF size");

    // Palette: 0 red, 1 green, 2 blue, 3 yellow; quadrants use indices 0..3.
    require(equals(image.pixel(0, 0), 255, 0, 0), "quadrant 0 red");
    require(equals(image.pixel(3, 0), 0, 255, 0), "quadrant 1 green");
    require(equals(image.pixel(0, 3), 0, 0, 255), "quadrant 2 blue");
    require(equals(image.pixel(3, 3), 255, 255, 0), "quadrant 3 yellow");

    std::vector<unsigned char> junk { 'n', 'o', 'p', 'e' };
    require(!decode_gif(junk, image), "reject non-GIF");

    return 0;
}

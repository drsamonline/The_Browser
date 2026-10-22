#include "png.hpp"
#include "png_test_data.h"

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

int main()
{
    Image image;

    // Colour type 2 (RGB).
    require(decode_png(bytes(kPngRgb, kPngRgb_len), image), "decode RGB");
    require(image.width == 2 && image.height == 2, "RGB size");
    require(image.pixel(0, 0).red == 255 && image.pixel(0, 0).green == 0, "RGB (0,0)");
    require(image.pixel(1, 1).red == 255 && image.pixel(1, 1).green == 255, "RGB (1,1)");

    // Colour type 6 (RGBA).
    require(decode_png(bytes(kPngRgba, kPngRgba_len), image), "decode RGBA");
    require(image.pixel(0, 0).alpha == 255, "RGBA opaque");
    require(image.pixel(1, 0).alpha == 128, "RGBA half");
    require(image.pixel(1, 1).red == 10 && image.pixel(1, 1).alpha == 40, "RGBA (1,1)");

    // Colour type 0 (grey).
    require(decode_png(bytes(kPngGrey, kPngGrey_len), image), "decode grey");
    require(image.pixel(0, 0).red == 0 && image.pixel(1, 1).red == 255, "grey ramp");

    // Colour type 3 (palette + tRNS).
    require(decode_png(bytes(kPngPalette, kPngPalette_len), image), "decode palette");
    require(image.pixel(0, 0).red == 255 && image.pixel(0, 0).alpha == 255, "palette entry 0");
    require(image.pixel(1, 1).green == 255 && image.pixel(1, 1).alpha == 0, "palette entry 3 + tRNS");

    // Interlaced (Adam7) must decode identically to the same non-interlaced image.
    Image interlaced;
    require(decode_png(bytes(kPngInterlaced, kPngInterlaced_len), interlaced), "decode interlaced PNG");
    Image plain;
    require(decode_png(bytes(kPngPlain, kPngPlain_len), plain), "decode plain PNG");
    require(interlaced.width == plain.width && interlaced.height == plain.height, "interlaced size");
    for (int y = 0; y < plain.height; ++y)
        for (int x = 0; x < plain.width; ++x) {
            auto a = interlaced.pixel(x, y);
            auto b = plain.pixel(x, y);
            require(a.red == b.red && a.green == b.green && a.blue == b.blue, "interlaced matches plain");
        }

    // Non-PNG input is rejected.
    std::vector<unsigned char> junk { 'P', '3', ' ', '2' };
    require(!decode_png(junk, image), "reject non-PNG");

    return 0;
}

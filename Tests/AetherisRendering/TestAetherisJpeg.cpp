#include "jpeg.hpp"
#include "jpeg_test_data.h"

#include <cmath>
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

// JPEG is lossy, so compare quadrant centres within a tolerance.
static bool near(int value, int expected, int tolerance = 30)
{
    return std::abs(value - expected) <= tolerance;
}

int main()
{
    Image image;

    require(decode_jpeg(bytes(kJpegRgb, kJpegRgb_len), image), "decode RGB JPEG");
    require(image.width == 8 && image.height == 8, "RGB JPEG size");

    auto red = image.pixel(1, 1);
    require(near(red.red, 220) && near(red.green, 30) && near(red.blue, 30), "red quadrant");
    auto green = image.pixel(6, 1);
    require(near(green.green, 200) && near(green.red, 30), "green quadrant");
    auto blue = image.pixel(1, 6);
    require(near(blue.blue, 220) && near(blue.red, 30), "blue quadrant");
    auto white = image.pixel(6, 6);
    require(near(white.red, 240) && near(white.green, 240) && near(white.blue, 240), "white quadrant");

    require(decode_jpeg(bytes(kJpegGrey, kJpegGrey_len), image), "decode grey JPEG");
    require(image.width == 8 && image.height == 8, "grey JPEG size");
    require(image.pixel(1, 1).red > 200, "grey bright side");
    require(image.pixel(6, 1).red < 60, "grey dark side");

    // Progressive JPEG must decode to the same quadrants as the baseline one.
    require(decode_jpeg(bytes(kJpegProgressive, kJpegProgressive_len), image), "decode progressive JPEG");
    require(image.width == 8 && image.height == 8, "progressive JPEG size");
    auto p_red = image.pixel(1, 1);
    require(near(p_red.red, 220) && near(p_red.green, 30) && near(p_red.blue, 30), "progressive red quadrant");
    auto p_green = image.pixel(6, 1);
    require(near(p_green.green, 200) && near(p_green.red, 30), "progressive green quadrant");
    auto p_blue = image.pixel(1, 6);
    require(near(p_blue.blue, 220) && near(p_blue.red, 30), "progressive blue quadrant");
    auto p_white = image.pixel(6, 6);
    require(near(p_white.red, 240) && near(p_white.green, 240) && near(p_white.blue, 240), "progressive white quadrant");

    std::vector<unsigned char> junk { 'x' };
    require(!decode_jpeg(junk, image), "reject non-JPEG");

    return 0;
}

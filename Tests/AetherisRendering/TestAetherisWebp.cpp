#include "webp.hpp"
#include "webp_test_data.h"

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

// The fixture is pixel (x, y) = ((x*60+10)%256, (y*60+10)%256, ((x+y)*30)%256).
static void check_pixel(Image const& image, int x, int y, int tolerance, char const* label)
{
    auto expected = std::make_tuple((x * 60 + 10) % 256, (y * 60 + 10) % 256, ((x + y) * 30) % 256);
    auto actual = image.pixel(x, y);
    require(std::abs(actual.red - std::get<0>(expected)) <= tolerance, label);
    require(std::abs(actual.green - std::get<1>(expected)) <= tolerance, label);
    require(std::abs(actual.blue - std::get<2>(expected)) <= tolerance, label);
}

int main()
{
    require(webp_available(), "WebP support is built in");

    Image image;
    require(is_webp(bytes(kWebpLossless, kWebpLossless_len)), "detect WebP");

    // Lossless VP8L must round-trip exactly.
    require(decode_webp(bytes(kWebpLossless, kWebpLossless_len), image), "decode lossless WebP");
    require(image.width == 4 && image.height == 4, "lossless size");
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            check_pixel(image, x, y, 0, "lossless pixel");

    // Lossy VP8 is approximate.
    require(decode_webp(bytes(kWebpLossy, kWebpLossy_len), image), "decode lossy WebP");
    require(image.width == 4 && image.height == 4, "lossy size");
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            check_pixel(image, x, y, 40, "lossy pixel");

    std::vector<unsigned char> junk { 'x' };
    require(!decode_webp(junk, image), "reject non-WebP");

    return 0;
}

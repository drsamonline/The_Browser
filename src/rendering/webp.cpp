#include "webp.hpp"

#include "color.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

#if !defined(AETHERIS_HAS_WEBP)
#define AETHERIS_HAS_WEBP 0
#endif

#if AETHERIS_HAS_WEBP
// The minimal libwebp decoder API. Declaring it here (rather than including
// <webp/decode.h>) keeps the build independent of where the headers live; the
// ABI is stable. This mirrors how upstream Ladybird links libwebp.
extern "C" {
int WebPGetInfo(uint8_t const* data, size_t data_size, int* width, int* height);
uint8_t* WebPDecodeBGRAInto(uint8_t const* data, size_t data_size, uint8_t* output_buffer, size_t output_buffer_size, int output_stride);
}
#endif

namespace aetheris::rendering {

bool is_webp(std::vector<unsigned char> const& bytes)
{
    return bytes.size() >= 12
        && std::memcmp(bytes.data(), "RIFF", 4) == 0
        && std::memcmp(bytes.data() + 8, "WEBP", 4) == 0;
}

bool webp_available()
{
#if AETHERIS_HAS_WEBP
    return true;
#else
    return false;
#endif
}

bool decode_webp(std::vector<unsigned char> const& bytes, Image& out)
{
#if AETHERIS_HAS_WEBP
    if (!is_webp(bytes))
        return false;

    int width = 0;
    int height = 0;
    if (WebPGetInfo(bytes.data(), bytes.size(), &width, &height) != 1)
        return false;
    if (width <= 0 || height <= 0)
        return false;

    std::vector<uint8_t> buffer(static_cast<size_t>(width) * height * 4);
    // WebPDecodeBGRAInto returns non-premultiplied BGRA.
    if (WebPDecodeBGRAInto(bytes.data(), bytes.size(), buffer.data(), buffer.size(), width * 4) == nullptr)
        return false;

    out = Image {};
    out.width = width;
    out.height = height;
    out.pixels.resize(static_cast<size_t>(width) * height);
    for (size_t i = 0; i < out.pixels.size(); ++i)
        out.pixels[i] = Color { buffer[i * 4 + 2], buffer[i * 4 + 1], buffer[i * 4 + 0], buffer[i * 4 + 3] };
    return true;
#else
    (void)bytes;
    (void)out;
    return false;
#endif
}

} // namespace aetheris::rendering

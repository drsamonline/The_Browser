#pragma once

#include "image.hpp"

#include <vector>

namespace aetheris::rendering {

// True if the bytes begin with a RIFF/WEBP container signature.
bool is_webp(std::vector<unsigned char> const& bytes);

// True when the build was linked against libwebp.
bool webp_available();

// Decodes a WebP image (lossy VP8 or lossless VP8L) into an Image. Returns
// false when the input is not WebP or WebP support was not compiled in.
bool decode_webp(std::vector<unsigned char> const& bytes, Image& out);

} // namespace aetheris::rendering

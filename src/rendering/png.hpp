#pragma once

#include "image.hpp"

#include <cstddef>
#include <vector>

namespace aetheris::rendering {

// Decodes a PNG into an Image. Supports bit depth 8 (and 16 by taking the high
// byte), colour types 0/2/3/4/6 (grey, RGB, palette, grey+alpha, RGBA), and
// non-interlaced images. Returns false for malformed or unsupported input.
bool decode_png(std::vector<unsigned char> const& bytes, Image& out);

// True if the bytes begin with the 8-byte PNG signature.
bool is_png(std::vector<unsigned char> const& bytes);

} // namespace aetheris::rendering

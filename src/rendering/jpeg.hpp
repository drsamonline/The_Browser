#pragma once

#include "image.hpp"

#include <vector>

namespace aetheris::rendering {

// Decodes a baseline (SOF0), 8-bit JPEG — 1 or 3 components — into an Image.
// Progressive and arithmetic-coded JPEGs are not supported. Returns false on
// malformed or unsupported input.
bool decode_jpeg(std::vector<unsigned char> const& bytes, Image& out);

// True if the bytes begin with the JPEG SOI marker (FF D8).
bool is_jpeg(std::vector<unsigned char> const& bytes);

} // namespace aetheris::rendering

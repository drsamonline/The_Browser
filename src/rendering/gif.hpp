#pragma once

#include "image.hpp"

#include <vector>

namespace aetheris::rendering {

// Decodes the first frame of a GIF (87a/89a) into an Image. LZW-compressed,
// palette-based, with global/local colour tables, interlacing and the graphic
// control extension's transparency index. Returns false on malformed input.
bool decode_gif(std::vector<unsigned char> const& bytes, Image& out);

// True if the bytes begin with a GIF87a or GIF89a signature.
bool is_gif(std::vector<unsigned char> const& bytes);

} // namespace aetheris::rendering

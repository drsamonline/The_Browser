#pragma once

#include "ttf_font.hpp"

namespace aetheris::rendering {

// The outline font used for text, when one can be found. Loaded once from
// AETHERIS_FONT or a short list of common system paths; null when no font is
// available, in which case the embedded bitmap font is used instead.
TtfFont const* active_font();

} // namespace aetheris::rendering

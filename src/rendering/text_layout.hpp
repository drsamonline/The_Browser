#pragma once

#include "layout.hpp"

#include <string>
#include <vector>

namespace aetheris::rendering {

class TextLayout {
public:
    static std::string collapse_whitespace(std::string_view text, bool preserve = false);
    static float font_size(StyleProperties const&);
    static float line_height(StyleProperties const&);
    static float measure_text(std::string_view text, float font_size, StyleProperties const& style);
    static std::vector<TextFragment> layout(std::string_view text, float x, float y, float available_width, StyleProperties const&);
};

} // namespace aetheris::rendering

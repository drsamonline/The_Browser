#pragma once

#include "layout.hpp"

#include <string>
#include <vector>

namespace aetheris::rendering {

struct TextFragment {
    std::string text;
    LayoutRect rect;
    float baseline { 0 };
};

class TextLayout {
public:
    static std::string collapse_whitespace(std::string_view text, bool preserve = false);
    static float font_size(StyleProperties const&);
    static float line_height(StyleProperties const&);
    static float measure_text(std::string_view text, float font_size);
    static std::vector<TextFragment> layout(std::string_view text, float x, float y, float available_width, StyleProperties const&);
};

} // namespace aetheris::rendering

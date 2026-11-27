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
    // `continuation_x` is where the run's lines after the first begin: a run that starts mid-line - after an
    // inline box on the same line - wraps its later lines at the containing block's left edge, not at its own
    // start (row 0.21). Negative means "same as `x`".
    //
    // `bands` are the floats whose line boxes this run has to avoid (css2 §9.5). They are a plain array, not a
    // callback: asking a std::function for each line of each run made a page layout take 4m40s.
    static std::vector<TextFragment> layout(std::string_view text, float x, float y, float available_width,
        StyleProperties const&, float continuation_x = -1.0f,
        FloatBox const* bands = nullptr, std::size_t band_count = 0);
};

} // namespace aetheris::rendering

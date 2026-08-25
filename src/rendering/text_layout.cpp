#include "text_layout.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace aetheris::rendering {

namespace {

float parse_px(std::string const* value, float fallback)
{
    if (!value || value->empty() || *value == "normal")
        return fallback;
    char* end = nullptr;
    auto parsed = std::strtof(value->c_str(), &end);
    return end != value->c_str() ? parsed : fallback;
}

}

std::string TextLayout::collapse_whitespace(std::string_view text, bool preserve)
{
    if (preserve)
        return std::string(text);

    std::string result;
    bool pending_space = false;
    for (unsigned char c : text) {
        if (std::isspace(c)) {
            pending_space = !result.empty();
            continue;
        }
        if (pending_space) {
            result += ' ';
            pending_space = false;
        }
        result += static_cast<char>(c);
    }
    return result;
}

float TextLayout::font_size(StyleProperties const& style)
{
    return parse_px(style.get("font-size"), 16.0f);
}

float TextLayout::line_height(StyleProperties const& style)
{
    auto size = font_size(style);
    auto value = style.get("line-height");
    if (!value || *value == "normal")
        return size * 1.2f;

    char* end = nullptr;
    auto parsed = std::strtof(value->c_str(), &end);
    if (end == value->c_str())
        return size * 1.2f;
    if (end && *end == '\0')
        return parsed * size;
    return parsed;
}

float TextLayout::measure_text(std::string_view text, float size)
{
    // Temporary deterministic metrics. Real font shaping/raster metrics are a later backend stage.
    float width = 0;
    for (unsigned char c : text) {
        if (c == ' ')
            width += size * 0.33f;
        else if (std::ispunct(c))
            width += size * 0.45f;
        else
            width += size * 0.60f;
    }
    return width;
}

std::vector<TextFragment> TextLayout::layout(std::string_view source, float x, float y, float available_width, StyleProperties const& style)
{
    std::vector<TextFragment> fragments;
    auto preserve = style.get("white-space") && *style.get("white-space") == "pre";
    auto text = collapse_whitespace(source, preserve);
    if (text.empty() || available_width <= 0)
        return fragments;

    auto size = font_size(style);
    auto height = line_height(style);
    float cursor_x = x;
    float cursor_y = y;

    std::istringstream words(text);
    std::string word;
    while (words >> word) {
        auto word_width = measure_text(word, size);
        auto space_width = cursor_x > x ? measure_text(" ", size) : 0;

        if (cursor_x > x && cursor_x + space_width + word_width > x + available_width) {
            cursor_x = x;
            cursor_y += height;
            space_width = 0;
        }

        if (word_width > available_width && cursor_x == x) {
            std::string chunk;
            for (char c : word) {
                auto candidate = chunk + c;
                if (!chunk.empty() && measure_text(candidate, size) > available_width) {
                    auto width = measure_text(chunk, size);
                    fragments.push_back({ chunk, { cursor_x, cursor_y, width, height }, cursor_y + size });
                    cursor_y += height;
                    chunk.clear();
                }
                chunk += c;
            }
            if (!chunk.empty()) {
                auto width = measure_text(chunk, size);
                fragments.push_back({ chunk, { cursor_x, cursor_y, width, height }, cursor_y + size });
                cursor_x += width;
            }
            continue;
        }

        if (space_width > 0)
            cursor_x += space_width;
        fragments.push_back({ word, { cursor_x, cursor_y, word_width, height }, cursor_y + size });
        cursor_x += word_width;
    }

    return fragments;
}

} // namespace aetheris::rendering

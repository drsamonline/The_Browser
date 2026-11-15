#pragma once

#include <cctype>
#include <cstdlib>
#include <string>

namespace aetheris::rendering {

// Resolves a CSS length to pixels. The engine had no unit support at all, so
// "2em" was read by strtof as 2 and an <h1> rendered at a 2px font size.
//
// em and rem are both resolved against `reference`, the root font size: true
// em resolution needs the parent's computed size threaded through the cascade,
// and using the root is right for headings, which is where em mostly appears.
inline float css_length_px(std::string const* value, float reference = 16.0f, float fallback = 16.0f)
{
    if (!value || value->empty())
        return fallback;

    std::string text;
    text.reserve(value->size());
    for (char c : *value) {
        if (!std::isspace(static_cast<unsigned char>(c)))
            text.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    char* end = nullptr;
    float number = std::strtof(text.c_str(), &end);
    if (end == text.c_str())
        return fallback;
    std::string unit(end);

    if (unit.empty())
        return number; // bare number, as in a unitless font-size
    if (unit == "px")
        return number;
    if (unit == "em" || unit == "rem")
        return number * reference;
    if (unit == "%")
        return number * reference / 100.0f;
    if (unit == "pt")
        return number * 4.0f / 3.0f;
    if (unit == "pc")
        return number * 16.0f;
    if (unit == "in")
        return number * 96.0f;
    if (unit == "cm")
        return number * 96.0f / 2.54f;
    if (unit == "mm")
        return number * 96.0f / 25.4f;
    return number; // unknown unit: treat as pixels rather than dropping it
}

} // namespace aetheris::rendering

#include "color.hpp"

#include <array>
#include <cctype>
#include <charconv>
#include <string>

namespace aetheris::rendering {

static std::optional<uint8_t> hex_byte(std::string_view value)
{
    if (value.size() != 2)
        return {};
    unsigned int result {};
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result, 16);
    if (ec != std::errc {} || ptr != value.data() + value.size() || result > 255)
        return {};
    return static_cast<uint8_t>(result);
}

std::optional<Color> Color::parse(std::string_view value)
{
    if (value == "transparent")
        return Color { 0, 0, 0, 0 };

    constexpr std::array named_colors {
        std::pair { "black", Color { 0, 0, 0, 255 } },
        std::pair { "white", Color { 255, 255, 255, 255 } },
        std::pair { "red", Color { 255, 0, 0, 255 } },
        std::pair { "green", Color { 0, 128, 0, 255 } },
        std::pair { "blue", Color { 0, 0, 255, 255 } },
        std::pair { "yellow", Color { 255, 255, 0, 255 } },
        std::pair { "gray", Color { 128, 128, 128, 255 } },
        std::pair { "grey", Color { 128, 128, 128, 255 } },
    };

    for (auto const& [name, color] : named_colors) {
        if (value == name)
            return color;
    }

    if (value.size() == 4 && value[0] == '#') {
        auto r = hex_byte(std::string { value.substr(1, 1) } + std::string { value.substr(1, 1) });
        auto g = hex_byte(std::string { value.substr(2, 1) } + std::string { value.substr(2, 1) });
        auto b = hex_byte(std::string { value.substr(3, 1) } + std::string { value.substr(3, 1) });
        if (r && g && b)
            return Color { *r, *g, *b, 255 };
    }

    if ((value.size() == 7 || value.size() == 9) && value[0] == '#') {
        auto r = hex_byte(value.substr(1, 2));
        auto g = hex_byte(value.substr(3, 2));
        auto b = hex_byte(value.substr(5, 2));
        auto a = value.size() == 9 ? hex_byte(value.substr(7, 2)) : std::optional<uint8_t> { 255 };
        if (r && g && b && a)
            return Color { *r, *g, *b, *a };
    }

    return {};
}

} // namespace aetheris::rendering

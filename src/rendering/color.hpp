#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace aetheris::rendering {

struct Color {
    uint8_t red { 0 };
    uint8_t green { 0 };
    uint8_t blue { 0 };
    uint8_t alpha { 255 };

    constexpr uint32_t rgba() const
    {
        return (static_cast<uint32_t>(red) << 24)
            | (static_cast<uint32_t>(green) << 16)
            | (static_cast<uint32_t>(blue) << 8)
            | alpha;
    }

    static std::optional<Color> parse(std::string_view);
    static Color blend_over(Color source, Color destination);
};

} // namespace aetheris::rendering

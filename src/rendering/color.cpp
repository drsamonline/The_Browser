#include "color.hpp"

#include <array>
#include <cctype>
#include <charconv>
#include <string>

#include "named_colors.inc"

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

    for (auto const& entry : s_named_colors) {
        if (value == entry.name) {
            return Color {
                static_cast<uint8_t>((entry.rgb >> 16) & 0xFF),
                static_cast<uint8_t>((entry.rgb >> 8) & 0xFF),
                static_cast<uint8_t>(entry.rgb & 0xFF),
                255
            };
        }
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


Color Color::blend_over(Color source, Color destination)
{
    auto source_alpha = static_cast<unsigned int>(source.alpha);
    auto destination_alpha = static_cast<unsigned int>(destination.alpha);
    auto inverse_source_alpha = 255u - source_alpha;
    auto out_alpha = source_alpha + (destination_alpha * inverse_source_alpha + 127) / 255;

    if (out_alpha == 0)
        return { 0, 0, 0, 0 };

    auto blend_channel = [&](uint8_t source_channel, uint8_t destination_channel) {
        auto source_premultiplied = static_cast<unsigned int>(source_channel) * source_alpha;
        auto destination_premultiplied = static_cast<unsigned int>(destination_channel) * destination_alpha * inverse_source_alpha / 255;
        return static_cast<uint8_t>((source_premultiplied + destination_premultiplied + out_alpha / 2) / out_alpha);
    };

    return {
        blend_channel(source.red, destination.red),
        blend_channel(source.green, destination.green),
        blend_channel(source.blue, destination.blue),
        static_cast<uint8_t>(out_alpha),
    };
}

} // namespace aetheris::rendering

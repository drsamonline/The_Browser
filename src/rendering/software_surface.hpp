#pragma once

#include "color.hpp"
#include "geometry.hpp"
#include "layout.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace aetheris::rendering {

class SoftwareSurface {
public:
    SoftwareSurface(int width, int height, Color clear_color = { 255, 255, 255, 255 });

    int width() const { return m_width; }
    int height() const { return m_height; }

    void clear(Color);
    void fill_rect(LayoutRect const&, Color, std::optional<RoundedRect> clip = {});
    void fill_rounded_rect(LayoutRect const&, float radius, Color, std::optional<RoundedRect> clip = {});
    void stroke_rect(LayoutRect const&, BoxEdges const&, Color, std::optional<RoundedRect> clip = {}, std::string_view style = "solid");
    void stroke_rounded_rect(LayoutRect const&, float radius, BoxEdges const&, Color, std::optional<RoundedRect> clip = {}, std::string_view style = "solid");
    void fill_text_cell(LayoutRect const&, Color, std::optional<RoundedRect> clip = {});

    Color pixel(int x, int y) const;
    std::vector<uint32_t> const& pixels() const { return m_pixels; }

private:
    bool contains(int x, int y) const;
    void blend_pixel(int x, int y, Color);
    bool accepts(int x, int y, std::optional<RoundedRect> const&) const;
    static std::optional<LayoutRect> clipped(LayoutRect const&, std::optional<RoundedRect> const&);

    int m_width { 0 };
    int m_height { 0 };
    std::vector<uint32_t> m_pixels;
};

} // namespace aetheris::rendering

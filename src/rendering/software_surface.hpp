#pragma once

#include "color.hpp"
#include "layout.hpp"

#include <cstdint>
#include <vector>

namespace aetheris::rendering {

class SoftwareSurface {
public:
    SoftwareSurface(int width, int height, Color clear_color = { 255, 255, 255, 255 });

    int width() const { return m_width; }
    int height() const { return m_height; }

    void clear(Color);
    void fill_rect(LayoutRect const&, Color);
    void fill_text_cell(LayoutRect const&, Color);

    Color pixel(int x, int y) const;
    std::vector<uint32_t> const& pixels() const { return m_pixels; }

private:
    bool contains(int x, int y) const;
    void set_pixel(int x, int y, Color);

    int m_width { 0 };
    int m_height { 0 };
    std::vector<uint32_t> m_pixels;
};

} // namespace aetheris::rendering

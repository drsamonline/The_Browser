#include "software_surface.hpp"

#include <algorithm>
#include <cmath>

namespace aetheris::rendering {

SoftwareSurface::SoftwareSurface(int width, int height, Color clear_color)
    : m_width(std::max(width, 0))
    , m_height(std::max(height, 0))
    , m_pixels(static_cast<size_t>(m_width) * static_cast<size_t>(m_height), clear_color.rgba())
{
}

void SoftwareSurface::clear(Color color)
{
    std::fill(m_pixels.begin(), m_pixels.end(), color.rgba());
}

bool SoftwareSurface::contains(int x, int y) const
{
    return x >= 0 && y >= 0 && x < m_width && y < m_height;
}

void SoftwareSurface::set_pixel(int x, int y, Color color)
{
    if (contains(x, y))
        m_pixels[static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x)] = color.rgba();
}

Color SoftwareSurface::pixel(int x, int y) const
{
    if (!contains(x, y))
        return {};
    auto value = m_pixels[static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x)];
    return {
        static_cast<uint8_t>(value >> 24),
        static_cast<uint8_t>(value >> 16),
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value),
    };
}

void SoftwareSurface::fill_rect(LayoutRect const& rect, Color color)
{
    auto left = static_cast<int>(std::floor(rect.x));
    auto top = static_cast<int>(std::floor(rect.y));
    auto right = static_cast<int>(std::ceil(rect.x + rect.width));
    auto bottom = static_cast<int>(std::ceil(rect.y + rect.height));

    left = std::clamp(left, 0, m_width);
    right = std::clamp(right, 0, m_width);
    top = std::clamp(top, 0, m_height);
    bottom = std::clamp(bottom, 0, m_height);

    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            set_pixel(x, y, color);
}

void SoftwareSurface::fill_text_cell(LayoutRect const& rect, Color color)
{
    // Deterministic temporary glyph-cell rasterization. Real glyph shaping/rasterization
    // will replace this backend hook without changing the paint command interface.
    auto glyph = rect;
    glyph.x += 1;
    glyph.y += 1;
    glyph.width = std::max(0.0f, glyph.width - 2);
    glyph.height = std::max(0.0f, glyph.height * 0.7f - 1);
    fill_rect(glyph, color);
}

} // namespace aetheris::rendering

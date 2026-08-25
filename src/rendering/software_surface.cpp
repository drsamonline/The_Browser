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

void SoftwareSurface::blend_pixel(int x, int y, Color color)
{
    if (!contains(x, y))
        return;
    auto index = static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x);
    auto old = m_pixels[index];
    Color destination {
        static_cast<uint8_t>(old >> 24),
        static_cast<uint8_t>(old >> 16),
        static_cast<uint8_t>(old >> 8),
        static_cast<uint8_t>(old),
    };
    m_pixels[index] = Color::blend_over(color, destination).rgba();
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

std::optional<LayoutRect> SoftwareSurface::clipped(LayoutRect const& rect, std::optional<LayoutRect> const& clip)
{
    if (!clip)
        return rect;
    auto left = std::max(rect.x, clip->x);
    auto top = std::max(rect.y, clip->y);
    auto right = std::min(rect.x + rect.width, clip->x + clip->width);
    auto bottom = std::min(rect.y + rect.height, clip->y + clip->height);
    if (right <= left || bottom <= top)
        return {};
    return LayoutRect { left, top, right - left, bottom - top };
}

void SoftwareSurface::fill_rect(LayoutRect const& source_rect, Color color, std::optional<LayoutRect> clip)
{
    auto clipped_rect = clipped(source_rect, clip);
    if (!clipped_rect)
        return;

    auto const& rect = *clipped_rect;
    auto left = std::clamp(static_cast<int>(std::floor(rect.x)), 0, m_width);
    auto top = std::clamp(static_cast<int>(std::floor(rect.y)), 0, m_height);
    auto right = std::clamp(static_cast<int>(std::ceil(rect.x + rect.width)), 0, m_width);
    auto bottom = std::clamp(static_cast<int>(std::ceil(rect.y + rect.height)), 0, m_height);

    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            blend_pixel(x, y, color);
}

void SoftwareSurface::stroke_rect(LayoutRect const& rect, BoxEdges const& edges, Color color, std::optional<LayoutRect> clip)
{
    if (edges.top > 0)
        fill_rect({ rect.x, rect.y, rect.width, edges.top }, color, clip);
    if (edges.bottom > 0)
        fill_rect({ rect.x, rect.y + rect.height - edges.bottom, rect.width, edges.bottom }, color, clip);
    if (edges.left > 0)
        fill_rect({ rect.x, rect.y, edges.left, rect.height }, color, clip);
    if (edges.right > 0)
        fill_rect({ rect.x + rect.width - edges.right, rect.y, edges.right, rect.height }, color, clip);
}

void SoftwareSurface::fill_text_cell(LayoutRect const& rect, Color color, std::optional<LayoutRect> clip)
{
    auto glyph = rect;
    glyph.x += 1;
    glyph.y += 1;
    glyph.width = std::max(0.0f, glyph.width - 2);
    glyph.height = std::max(0.0f, glyph.height * 0.7f - 1);
    fill_rect(glyph, color, clip);
}

} // namespace aetheris::rendering

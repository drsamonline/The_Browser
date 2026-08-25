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

std::optional<LayoutRect> SoftwareSurface::clipped(LayoutRect const& rect, std::optional<RoundedRect> const& clip)
{
    if (!clip)
        return rect;
    auto result = intersect_rect(rect, clip->rect);
    if (result.width <= 0 || result.height <= 0)
        return {};
    return result;
}

bool SoftwareSurface::accepts(int x, int y, std::optional<RoundedRect> const& clip) const
{
    return !clip || clip->contains(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
}

void SoftwareSurface::fill_rect(LayoutRect const& source_rect, Color color, std::optional<RoundedRect> clip)
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
            if (accepts(x, y, clip))
                blend_pixel(x, y, color);
}

void SoftwareSurface::fill_rounded_rect(LayoutRect const& rect, float radius, Color color, std::optional<RoundedRect> clip)
{
    RoundedRect shape { rect, radius };
    auto bounds = clipped(rect, clip);
    if (!bounds)
        return;

    auto left = std::clamp(static_cast<int>(std::floor(bounds->x)), 0, m_width);
    auto top = std::clamp(static_cast<int>(std::floor(bounds->y)), 0, m_height);
    auto right = std::clamp(static_cast<int>(std::ceil(bounds->x + bounds->width)), 0, m_width);
    auto bottom = std::clamp(static_cast<int>(std::ceil(bounds->y + bounds->height)), 0, m_height);

    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            if (shape.contains(x + 0.5f, y + 0.5f) && accepts(x, y, clip))
                blend_pixel(x, y, color);
}

void SoftwareSurface::stroke_rect(LayoutRect const& rect, BoxEdges const& edges, Color color, std::optional<RoundedRect> clip, std::string_view style)
{
    auto paint = [&](LayoutRect const& piece, int offset) {
        if (style == "dashed" || style == "dotted") {
            int step = style == "dashed" ? 6 : 3;
            for (int y = static_cast<int>(piece.y); y < static_cast<int>(piece.y + piece.height); ++y)
                for (int x = static_cast<int>(piece.x); x < static_cast<int>(piece.x + piece.width); ++x)
                    if (((x + y + offset) / step) % 2 == 0 && accepts(x, y, clip))
                        blend_pixel(x, y, color);
            return;
        }
        fill_rect(piece, color, clip);
    };

    if (edges.top > 0) paint({ rect.x, rect.y, rect.width, edges.top }, 0);
    if (edges.bottom > 0) paint({ rect.x, rect.y + rect.height - edges.bottom, rect.width, edges.bottom }, 1);
    if (edges.left > 0) paint({ rect.x, rect.y, edges.left, rect.height }, 2);
    if (edges.right > 0) paint({ rect.x + rect.width - edges.right, rect.y, edges.right, rect.height }, 3);
}

void SoftwareSurface::stroke_rounded_rect(LayoutRect const& rect, float radius, BoxEdges const& edges, Color color, std::optional<RoundedRect> clip, std::string_view style)
{
    float width = std::max({ edges.top, edges.right, edges.bottom, edges.left });
    if (width <= 0)
        return;

    RoundedRect outer { rect, radius };
    LayoutRect inner_rect {
        rect.x + width,
        rect.y + width,
        std::max(0.0f, rect.width - 2 * width),
        std::max(0.0f, rect.height - 2 * width)
    };
    RoundedRect inner { inner_rect, std::max(0.0f, radius - width) };

    auto left = std::clamp(static_cast<int>(std::floor(rect.x)), 0, m_width);
    auto top = std::clamp(static_cast<int>(std::floor(rect.y)), 0, m_height);
    auto right = std::clamp(static_cast<int>(std::ceil(rect.x + rect.width)), 0, m_width);
    auto bottom = std::clamp(static_cast<int>(std::ceil(rect.y + rect.height)), 0, m_height);

    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            bool dash = style == "solid" || (((x + y) / (style == "dashed" ? 6 : 3)) % 2 == 0);
            if (dash && outer.contains(x + 0.5f, y + 0.5f) && !inner.contains(x + 0.5f, y + 0.5f) && accepts(x, y, clip))
                blend_pixel(x, y, color);
        }
    }
}

void SoftwareSurface::fill_text_cell(LayoutRect const& rect, Color color, std::optional<RoundedRect> clip)
{
    auto glyph = rect;
    glyph.x += 1;
    glyph.y += 1;
    glyph.width = std::max(0.0f, glyph.width - 2);
    glyph.height = std::max(0.0f, glyph.height * 0.7f - 1);
    fill_rect(glyph, color, clip);
}

} // namespace aetheris::rendering

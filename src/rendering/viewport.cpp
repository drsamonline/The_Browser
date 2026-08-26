#include "viewport.hpp"

#include <algorithm>

namespace aetheris::rendering {

void Viewport::set_size(float width, float height)
{
    m_width = std::max(0.0f, width);
    m_height = std::max(0.0f, height);
    clamp_scroll();
}

void Viewport::set_document_size(float width, float height)
{
    m_document_width = std::max(0.0f, width);
    m_document_height = std::max(0.0f, height);
    clamp_scroll();
}

void Viewport::scroll_to(float x, float y)
{
    m_scroll_x = x;
    m_scroll_y = y;
    clamp_scroll();
}

void Viewport::scroll_by(float dx, float dy)
{
    scroll_to(m_scroll_x + dx, m_scroll_y + dy);
}

ViewportPoint Viewport::to_document_coordinates(float viewport_x, float viewport_y) const
{
    return { viewport_x + m_scroll_x, viewport_y + m_scroll_y };
}

ViewportPoint Viewport::to_viewport_coordinates(float document_x, float document_y) const
{
    return { document_x - m_scroll_x, document_y - m_scroll_y };
}

void Viewport::clamp_scroll()
{
    m_scroll_x = std::clamp(m_scroll_x, 0.0f, std::max(0.0f, m_document_width - m_width));
    m_scroll_y = std::clamp(m_scroll_y, 0.0f, std::max(0.0f, m_document_height - m_height));
}

} // namespace aetheris::rendering

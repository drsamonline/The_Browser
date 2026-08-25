#include "clip.hpp"

#include <algorithm>

namespace aetheris::rendering {

void ClipStack::push(LayoutRect rect)
{
    if (auto parent = current()) {
        auto left = std::max(parent->x, rect.x);
        auto top = std::max(parent->y, rect.y);
        auto right = std::min(parent->x + parent->width, rect.x + rect.width);
        auto bottom = std::min(parent->y + parent->height, rect.y + rect.height);
        rect = { left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top) };
    }
    m_stack.push_back(rect);
}

void ClipStack::pop()
{
    if (!m_stack.empty())
        m_stack.pop_back();
}

std::optional<LayoutRect> ClipStack::current() const
{
    if (m_stack.empty())
        return {};
    return m_stack.back();
}

std::optional<LayoutRect> ClipStack::intersect(LayoutRect const& rect) const
{
    if (!current())
        return rect;
    auto clip = *current();
    auto left = std::max(clip.x, rect.x);
    auto top = std::max(clip.y, rect.y);
    auto right = std::min(clip.x + clip.width, rect.x + rect.width);
    auto bottom = std::min(clip.y + clip.height, rect.y + rect.height);
    if (right <= left || bottom <= top)
        return {};
    return LayoutRect { left, top, right - left, bottom - top };
}

} // namespace aetheris::rendering

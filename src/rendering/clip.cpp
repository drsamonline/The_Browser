#include "clip.hpp"

namespace aetheris::rendering {

void ClipStack::push(LayoutRect rect, float radius)
{
    RoundedRect next { rect, radius };
    if (auto parent = current()) {
        next.rect = intersect_rect(parent->rect, next.rect);
        next.radius = std::min(parent->radius, next.radius);
    }
    m_stack.push_back(next.normalized());
}

void ClipStack::pop()
{
    if (!m_stack.empty())
        m_stack.pop_back();
}

std::optional<RoundedRect> ClipStack::current() const
{
    if (m_stack.empty())
        return {};
    return m_stack.back();
}

} // namespace aetheris::rendering

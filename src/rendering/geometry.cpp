#include "geometry.hpp"

#include <algorithm>

namespace aetheris::rendering {

RoundedRect RoundedRect::normalized() const
{
    RoundedRect result = *this;
    result.radius = std::clamp(radius, 0.0f, std::min(rect.width, rect.height) * 0.5f);
    return result;
}

bool RoundedRect::contains(float x, float y) const
{
    auto value = normalized();
    auto const& r = value.rect;
    if (x < r.x || y < r.y || x >= r.x + r.width || y >= r.y + r.height)
        return false;

    if (value.radius <= 0)
        return true;

    float radius = value.radius;
    float cx = std::clamp(x, r.x + radius, r.x + r.width - radius);
    float cy = std::clamp(y, r.y + radius, r.y + r.height - radius);
    float dx = x - cx;
    float dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

} // namespace aetheris::rendering

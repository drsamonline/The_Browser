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

    // `rad`, not `radius`: this is a member function and RoundedRect::radius is in
    // scope, so a local called `radius` shadows it (MSVC C4458, fatal under /WX).
    float rad = value.radius;
    float cx = std::clamp(x, r.x + rad, r.x + r.width - rad);
    float cy = std::clamp(y, r.y + rad, r.y + r.height - rad);
    float dx = x - cx;
    float dy = y - cy;
    return dx * dx + dy * dy <= rad * rad;
}

} // namespace aetheris::rendering

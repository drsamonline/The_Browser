#pragma once

#include "layout.hpp"

#include <algorithm>

namespace aetheris::rendering {

struct RoundedRect {
    LayoutRect rect;
    float radius { 0 };

    bool contains(float x, float y) const;
    RoundedRect normalized() const;
};

inline LayoutRect intersect_rect(LayoutRect const& a, LayoutRect const& b)
{
    auto left = std::max(a.x, b.x);
    auto top = std::max(a.y, b.y);
    auto right = std::min(a.x + a.width, b.x + b.width);
    auto bottom = std::min(a.y + a.height, b.y + b.height);
    if (right <= left || bottom <= top)
        return {};
    return { left, top, right - left, bottom - top };
}

} // namespace aetheris::rendering

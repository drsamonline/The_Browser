#pragma once

#include "geometry.hpp"

#include <optional>
#include <vector>

namespace aetheris::rendering {

class ClipStack {
public:
    void push(LayoutRect, float radius = 0);
    void pop();
    std::optional<RoundedRect> current() const;

private:
    std::vector<RoundedRect> m_stack;
};

} // namespace aetheris::rendering

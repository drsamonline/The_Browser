#pragma once

#include "layout.hpp"

#include <optional>
#include <vector>

namespace aetheris::rendering {

class ClipStack {
public:
    void push(LayoutRect);
    void pop();
    std::optional<LayoutRect> current() const;
    std::optional<LayoutRect> intersect(LayoutRect const&) const;

private:
    std::vector<LayoutRect> m_stack;
};

} // namespace aetheris::rendering

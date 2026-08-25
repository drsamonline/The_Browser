#pragma once

#include "layout.hpp"

#include <memory>
#include <string>
#include <vector>

namespace aetheris::rendering {

struct PaintCommand {
    enum class Type {
        FillRect,
        DrawText,
    };

    Type type { Type::FillRect };
    LayoutRect rect;
    std::string text;
    std::string color;
};

class RenderTree {
public:
    static RenderTree from_layout(LayoutNode const& root);

    std::vector<PaintCommand> const& commands() const { return m_commands; }

private:
    static void emit(LayoutNode const& node, std::vector<PaintCommand>& commands);

    std::vector<PaintCommand> m_commands;
};

} // namespace aetheris::rendering

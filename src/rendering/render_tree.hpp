#pragma once

#include "layout.hpp"

#include <string>
#include <vector>

namespace aetheris::rendering {

struct PaintCommand {
    enum class Type {
        FillRect,
        StrokeRect,
        DrawText,
    };

    Type type { Type::FillRect };
    LayoutRect rect;
    BoxEdges edges;
    std::string text;
    std::string color;
    float baseline { 0 };
    bool push_clip { false };
    bool pop_clip { false };
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

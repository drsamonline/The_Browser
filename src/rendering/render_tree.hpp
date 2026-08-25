#pragma once

#include "layout.hpp"
#include "image.hpp"

#include <string>
#include <vector>

namespace aetheris::rendering {

struct PaintCommand {
    enum class Type {
        FillRect,
        FillRoundedRect,
        StrokeRect,
        StrokeRoundedRect,
        DrawText,
        DrawImage,
    };

    Type type { Type::FillRect };
    LayoutRect rect;
    BoxEdges edges;
    std::string text;
    std::string color;
    std::string border_style { "solid" };
    float baseline { 0 };
    float radius { 0 };
    std::shared_ptr<Image const> image;
    float opacity { 1 };
    bool push_clip { false };
    bool pop_clip { false };
};

class RenderTree {
public:
    static RenderTree from_layout(LayoutNode const& root);

    std::vector<PaintCommand> const& commands() const { return m_commands; }

private:
    static void emit(LayoutNode const& node, std::vector<PaintCommand>& commands, float inherited_opacity);

    std::vector<PaintCommand> m_commands;
};

} // namespace aetheris::rendering

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
        DrawTextDecoration,
        DrawImage,
        DrawShadow,
        DrawOutline,
        PushStackingContext,
        PopStackingContext,
    };

    Type type { Type::FillRect };
    LayoutRect rect;
    BoxEdges edges;
    std::string text;
    std::string color;
    std::string border_style { "solid" };
    float baseline { 0 };
    std::string text_decoration { "none" };
    float radius { 0 };
    std::shared_ptr<Image const> image;
    float shadow_offset_x { 0 };
    float shadow_offset_y { 0 };
    float shadow_blur { 0 };
    std::string image_fit { "fill" };
    std::string background_repeat { "no-repeat" };
    std::string background_position { "0 0" };
    std::string background_size { "auto" };
    int z_index { 0 };
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

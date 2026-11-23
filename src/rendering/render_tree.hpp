#pragma once

#include "layout.hpp"
#include "image.hpp"

#include <string>
#include <vector>

namespace aetheris::rendering {

// A `position: sticky` box, and the range of paint commands that belong to it. The shift depends on the scroll
// offset, which changes without a relayout, so it is applied at paint time rather than baked into the geometry
// here (row 3b). `node_top`/`node_bottom` and the containing block's bounds are in document coordinates.
struct StickyRegion {
    std::size_t begin { 0 };
    std::size_t end { 0 };
    float node_top { 0 };
    float node_height { 0 };
    float containing_top { 0 };
    float containing_bottom { 0 };
    float top { 0 }; // `top: <px>`, in effect when has_top
    bool has_top { false };
    float bottom { 0 }; // `bottom: <px>`, in effect when has_bottom
    bool has_bottom { false };
};

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
    float font_size { 16 };
    // Carried so the pen advances by what layout measured: the shaper applies spacing to the glyph
    // advances, and a run shaped without it would draw tighter than its box (row 2g).
    float letter_spacing { 0 };
    float word_spacing { 0 };
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

// Cumulative CSS transform (translate + scale) applied while emitting commands.
struct PaintTransform {
    float tx { 0 };
    float ty { 0 };
    float sx { 1 };
    float sy { 1 };
};

class RenderTree {
public:
    static RenderTree from_layout(LayoutNode const& root);

    std::vector<PaintCommand> const& commands() const { return m_commands; }
    // `position: sticky` boxes, each with the command range it owns. Empty for a page without one.
    std::vector<StickyRegion> const& sticky_regions() const { return m_sticky_regions; }

private:
    static void emit(LayoutNode const& node, std::vector<PaintCommand>& commands, float inherited_opacity,
        PaintTransform const& inherited, std::vector<StickyRegion>& sticky_regions);

    std::vector<PaintCommand> m_commands;
    std::vector<StickyRegion> m_sticky_regions;
};

} // namespace aetheris::rendering

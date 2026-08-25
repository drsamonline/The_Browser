#pragma once

#include "dom.hpp"
#include "style.hpp"
#include "image.hpp"

#include <memory>
#include <string>
#include <vector>

namespace aetheris::rendering {

struct LayoutRect {
    float x { 0 };
    float y { 0 };
    float width { 0 };
    float height { 0 };
};

struct BoxEdges {
    float top { 0 };
    float right { 0 };
    float bottom { 0 };
    float left { 0 };
};

struct BoxModel {
    LayoutRect content;
    BoxEdges margin;
    BoxEdges padding;
    BoxEdges border;
};

enum class LayoutDisplay {
    Block,
    Inline,
    InlineBlock,
    None,
};

struct TextFragment {
    std::string text;
    LayoutRect rect;
    float baseline { 0 };
};

struct LayoutNode {
    DomNode const* dom_node { nullptr };
    StyleProperties style;
    LayoutDisplay display { LayoutDisplay::Block };
    LayoutRect rect;
    BoxModel box;
    std::vector<TextFragment> text_fragments;
    std::shared_ptr<Image const> image;
    std::shared_ptr<Image const> background_image;
    std::vector<std::unique_ptr<LayoutNode>> children;
    LayoutNode* parent { nullptr };

    LayoutNode& append_child(std::unique_ptr<LayoutNode> child);
};

class LayoutTreeBuilder {
public:
    std::unique_ptr<LayoutNode> build(DomNode const& document, CssStyleSheet const& sheet) const;

private:
    std::unique_ptr<LayoutNode> build_node(DomNode const& node, CssStyleSheet const& sheet, LayoutNode* parent, StyleProperties const* parent_style) const;
    static LayoutDisplay display_for(DomNode const& node, StyleProperties const& style);
};

class LayoutEngine {
public:
    void layout(LayoutNode& root, float viewport_width) const;

private:
    static void layout_node(LayoutNode& node, float x, float y, float available_width);
    static void layout_inline_children(LayoutNode& node, float x, float y, float available_width, float& cursor_y);
    static void layout_absolute_children(LayoutNode& node, float available_width);
    static float parse_length(std::string const* value, float fallback, float percentage_base = 0);
    static float resolve_dimension(StyleProperties const&, char const* property, float available_width, float fallback);
    static BoxEdges resolve_edges(StyleProperties const& style, char const* prefix, float percentage_base);
    static bool is_positioned(LayoutNode const& node);
};

} // namespace aetheris::rendering

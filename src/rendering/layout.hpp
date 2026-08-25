#pragma once

#include "dom.hpp"
#include "style.hpp"

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
    None,
};

struct LayoutNode {
    DomNode const* dom_node { nullptr };
    StyleProperties style;
    LayoutDisplay display { LayoutDisplay::Block };
    LayoutRect rect;
    BoxModel box;
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
    static float parse_length(std::string const* value, float fallback);
    static BoxEdges resolve_edges(StyleProperties const& style, char const* prefix);
};

} // namespace aetheris::rendering

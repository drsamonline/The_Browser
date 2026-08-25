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
    std::vector<std::unique_ptr<LayoutNode>> children;
    LayoutNode* parent { nullptr };

    LayoutNode& append_child(std::unique_ptr<LayoutNode> child);
};

class LayoutTreeBuilder {
public:
    std::unique_ptr<LayoutNode> build(DomNode const& document, CssStyleSheet const& sheet) const;

private:
    std::unique_ptr<LayoutNode> build_node(DomNode const& node, CssStyleSheet const& sheet, LayoutNode* parent) const;
    static LayoutDisplay display_for(DomNode const& node, StyleProperties const& style);
};

class LayoutEngine {
public:
    void layout(LayoutNode& root, float viewport_width) const;

private:
    static void layout_node(LayoutNode& node, float x, float y, float available_width);
    static float parse_length(std::string const* value, float fallback);
    static float resolve_vertical_spacing(StyleProperties const& style, char const* property);
};

} // namespace aetheris::rendering

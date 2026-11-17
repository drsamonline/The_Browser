#pragma once

#include "dom.hpp"
#include "style.hpp"

#include <unordered_map>
#include <unordered_set>
#include "visual_state.hpp"
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
    Flex,
    Grid,
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
    // `restyled`, when given, receives every node whose style was resolved rather than reused.
    // That set is exactly the nodes whose resolved style can differ from last time, so it is
    // what the layout-key comparison has to look at - everything else kept the properties the
    // last snapshot digested.
    std::unique_ptr<LayoutNode> build(DomNode const& document, CssStyleSheet const& sheet,
        VisualInteractionState const* interaction_state = nullptr,
        std::unordered_map<DomNode const*, StyleCacheEntry>* cache = nullptr,
        std::unordered_set<DomNode const*> const* dirty = nullptr,
        StyleCacheStats* stats = nullptr,
        std::unordered_set<DomNode const*>* restyled = nullptr) const;

private:
    std::unique_ptr<LayoutNode> build_node(DomNode const& node, CssStyleSheet const& sheet, LayoutNode* parent,
        StyleProperties const* parent_style, VisualInteractionState const* interaction_state,
        std::unordered_map<DomNode const*, StyleCacheEntry>* cache,
        std::unordered_set<DomNode const*> const* dirty, StyleCacheStats* stats,
        std::unordered_set<DomNode const*>* restyled) const;
    static LayoutDisplay display_for(DomNode const& node, StyleProperties const& style);
};

class LayoutEngine {
public:
    void layout(LayoutNode& root, float viewport_width) const;

private:
    static void layout_node(LayoutNode& node, float x, float y, float available_width);
    static void layout_inline_children(LayoutNode& node, float x, float y, float available_width, float& cursor_y);
    static void layout_absolute_children(LayoutNode& node, float available_width);
    static void layout_flex_children(LayoutNode& node, float available_width, float& cursor_y);
    static void layout_grid_children(LayoutNode& node, float available_width, float& cursor_y);
    static float parse_length(std::string const* value, float fallback, float percentage_base = 0);
    static float resolve_dimension(StyleProperties const&, char const* property, float available_width, float fallback);
    static BoxEdges resolve_edges(StyleProperties const& style, char const* prefix, float percentage_base);
    static bool is_positioned(LayoutNode const& node);
};

} // namespace aetheris::rendering

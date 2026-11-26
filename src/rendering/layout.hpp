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

// The geometry one layout pass produced for one DOM node, plus the box it was produced for.
// The origin and available width are part of the key: a subtree's geometry is only reusable
// where it was laid out, so a node that moves is laid out again even if nothing restyled.
struct CachedGeometry {
    LayoutRect rect;
    BoxModel box;
    std::vector<TextFragment> fragments;
    float x { 0 };
    float y { 0 };
    float available_width { 0 };
};

struct LayoutNode {
    DomNode const* dom_node { nullptr };
    StyleProperties style;
    LayoutDisplay display { LayoutDisplay::Block };
    LayoutRect rect;
    BoxModel box;
    std::vector<TextFragment> text_fragments;

    // Set on a box generated for `::before` or `::after`, which has no DOM node of its own.
    // `is_generated` is separate from the text being non-empty: `content: ''` generates a box with no
    // text, and using the text as the marker made that box invisible.
    bool is_generated { false };
    std::string generated_text;
    // `content: "x" / "alt"` - the part after the slash. Nothing reads it yet; see layout.cpp.
    std::string generated_alt_text;

    std::shared_ptr<Image const> image;
    std::shared_ptr<Image const> background_image;
    std::vector<std::unique_ptr<LayoutNode>> children;
    LayoutNode* parent { nullptr };

    // The origin this node was laid out at, recorded by the pass so the next one can ask
    // whether the subtree's geometry is still valid where it now sits.
    float layout_x { 0 };
    float layout_y { 0 };
    float layout_available_width { 0 };

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

// What a layout pass may reuse from the last one. Null on the engine means "lay it all out",
// which is what the first pass and any structural change do.
struct LayoutReuse {
    // Geometry from the last pass, keyed by DOM node.
    std::unordered_map<DomNode const*, CachedGeometry> const* geometry { nullptr };
    // Nodes whose subtree a restyle reached: they, and everything above them, are laid out
    // again. A node outside this set has the same style it had last time.
    std::unordered_set<DomNode const*> const* dirty { nullptr };
    // Bumped for every node the engine actually laid out, so the summary can report the cost.
    std::size_t* laid_out { nullptr };
};

class LayoutEngine {
public:
    explicit LayoutEngine(LayoutReuse const* reuse = nullptr)
        : m_reuse(reuse)
    {
    }

    void layout(LayoutNode& root, float viewport_width) const;

private:
    // True when the subtree's geometry from the last pass is still valid at this origin: the
    // node is clean, and it sits where it sat. `layout_node` then copies the recorded geometry
    // for the whole subtree instead of laying it out.
public:
    // "How wide does this box want to be?" - the max-content width the flex sizing uses. Exposed so the
    // layout probe can compare it with what the box actually gets, which is how a zero-width flex item is
    // traced back to the node it started at (row 0.13).
    static float intrinsic_width(LayoutNode const& node) { return max_content_width(node); }
    // "How narrow may this box be?" - the automatic minimum size a flex item's shrink is floored at. Exposed
    // for the same reason as `intrinsic_width`: an item that overflows is either larger than its base size or
    // floored above the space it was given, and only the pair of numbers can say which.
    static float intrinsic_min_width(LayoutNode const& node) { return min_content_width(node); }

private:
    bool reuse_subtree(LayoutNode& node, float x, float y, float available_width) const;

    void layout_node(LayoutNode& node, float x, float y, float available_width) const;
    void layout_inline_children(LayoutNode& node, float x, float y, float available_width, float& cursor_y) const;
    void layout_absolute_children(LayoutNode& node, float available_width) const;
    void layout_flex_children(LayoutNode& node, float available_width, float& cursor_y) const;
    void layout_grid_children(LayoutNode& node, float available_width, float& cursor_y) const;
    static float parse_length(std::string const* value, float fallback, float percentage_base = 0);
    // The widest a node's content can be: a text run's measured width, or the widest (or, for a row of inline
    // children, the sum) of its children's. Used as the flex base size when an item names neither a basis nor
    // a width, which this engine had no answer for (row 0.10).
    static float max_content_width(LayoutNode const& node);
    // The narrowest a box can be before its content overflows - the automatic minimum size a flex item's
    // shrink is floored at.
    static float min_content_width(LayoutNode const& node);
    static float resolve_dimension(StyleProperties const&, char const* property, float available_width, float fallback);
    static BoxEdges resolve_edges(StyleProperties const& style, char const* prefix, float percentage_base);
    static bool is_positioned(LayoutNode const& node);

    LayoutReuse const* m_reuse { nullptr };
};

} // namespace aetheris::rendering

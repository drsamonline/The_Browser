#include "layout.hpp"
#include "content_value.hpp"

#include <vector>

#include "text_layout.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace aetheris::rendering {

LayoutNode& LayoutNode::append_child(std::unique_ptr<LayoutNode> child)
{
    child->parent = this;
    children.push_back(std::move(child));
    return *children.back();
}

std::unique_ptr<LayoutNode> LayoutTreeBuilder::build(DomNode const& document, CssStyleSheet const& sheet,
    VisualInteractionState const* interaction_state,
    std::unordered_map<DomNode const*, StyleCacheEntry>* cache,
    std::unordered_set<DomNode const*> const* dirty, StyleCacheStats* stats,
    std::unordered_set<DomNode const*>* restyled) const
{
    return build_node(document, sheet, nullptr, nullptr, interaction_state, cache, dirty, stats, restyled);
}

std::unique_ptr<LayoutNode> LayoutTreeBuilder::build_node(DomNode const& node, CssStyleSheet const& sheet, LayoutNode* parent,
    StyleProperties const* parent_style, VisualInteractionState const* interaction_state,
    std::unordered_map<DomNode const*, StyleCacheEntry>* cache,
    std::unordered_set<DomNode const*> const* dirty, StyleCacheStats* stats,
    std::unordered_set<DomNode const*>* restyled) const
{
    auto layout_node = std::make_unique<LayoutNode>();
    layout_node->dom_node = &node;
    layout_node->parent = parent;

    // A clean node whose parent resolves to the same style as last time resolves to the
    // same style as last time, so the selector matching can be skipped. Anything dirty,
    // or with a changed parent, is resolved again - inheritance makes the parent the
    // only input that can change without the node itself changing.
    auto const parent_fingerprint = parent_style ? parent_style->fingerprint() : std::string {};
    bool reused = false;
    if (cache && dirty && !dirty->count(&node)) {
        auto cached = cache->find(&node);
        if (cached != cache->end() && cached->second.parent_fingerprint == parent_fingerprint) {
            layout_node->style = cached->second.properties;
            reused = true;
            if (stats)
                ++stats->hits;
        }
    }
    if (!reused) {
        layout_node->style = StyleResolver {}.resolve(node, sheet, parent_style, interaction_state);
        if (stats)
            ++stats->resolves;
        if (restyled)
            restyled->insert(&node);
    }
    if (cache)
        (*cache)[&node] = StyleCacheEntry { parent_fingerprint, layout_node->style };

    layout_node->display = display_for(node, layout_node->style);

    // display: none hides the whole subtree. Hoisting the descendants into the
    // parent (as this used to do) is not what CSS does, and it made <head>
    // contents such as <title> render as page text.
    if (layout_node->display == LayoutDisplay::None)
        return layout_node;

    // `::before` and `::after` generate a box only when `content` is a string: upstream's
    // PseudoElementDecision rule returns None for `normal` and `none`
    // (Rust/src/layout/tree_builder.rs). The box is a layout-tree child of the element, first or last,
    // and has no DOM node.
    auto make_generated_box = [&](char const* pseudo_element) -> std::unique_ptr<LayoutNode> {
        if (!sheet.uses_generated_content() || node.type != DomNodeType::Element)
            return nullptr;
        auto pseudo_style = StyleResolver {}.resolve_pseudo_element(node, sheet, pseudo_element,
            &layout_node->style, interaction_state);
        auto const* content = pseudo_style.get("content");
        if (!content)
            return nullptr;
        // The declaration is a list of items plus optional alt text, not a string: `content: "a" "b"`
        // renders "ab", and everything after `/` is alternative text rather than page text. Upstream asks
        // the same question of a ContentStyleValue (CSS/StyleValues/ContentStyleValue.h) and gets back
        // Normal, None or List (Rust/src/layout/tree_builder.rs:1714).
        auto const content_value = parse_content_value(*content);
        if (!content_value.generates_box())
            return nullptr;
        auto generated = std::make_unique<LayoutNode>();
        generated->parent = layout_node.get();
        generated->style = pseudo_style;
        // The box's display comes from its own style, not a hardcoded inline: a `::before` with
        // `display: inline-block; width: 60px` is a box of that size, and forcing Inline made every
        // generated box zero-width when its content was empty.
        generated->display = LayoutDisplay::Inline;
        if (auto const* display = pseudo_style.get("display")) {
            if (*display == "none")
                return nullptr;
            if (*display == "block")
                generated->display = LayoutDisplay::Block;
            else if (*display == "inline-block")
                generated->display = LayoutDisplay::InlineBlock;
            else if (*display == "flex" || *display == "inline-flex")
                generated->display = LayoutDisplay::Flex;
            else if (*display == "grid" || *display == "inline-grid")
                generated->display = LayoutDisplay::Grid;
        }
        generated->is_generated = true;
        generated->generated_text = content_value.text();
        // Nothing consumes this yet: there is no accessibility tree (tier 4). It is kept on the node so the
        // alt text is not thrown away between the parser and the day something asks for it.
        generated->generated_alt_text = content_value.alt();
        return generated;
    };

    if (auto before_box = make_generated_box("before"))
        layout_node->append_child(std::move(before_box));

    for (auto const& child : node.children)
        layout_node->append_child(build_node(*child, sheet, layout_node.get(), &layout_node->style, interaction_state, cache, dirty, stats, restyled));

    if (auto after_box = make_generated_box("after"))
        layout_node->append_child(std::move(after_box));
    if (std::getenv("AETHERIS_TRACE_GENERATED")) {
        for (auto const& child : layout_node->children)
            std::fprintf(stderr, "GEN child gen=[%s] display=%d frags=%zu rect=(%.1f,%.1f %.1fx%.1f)\n",
                child->generated_text.c_str(), static_cast<int>(child->display),
                child->text_fragments.size(), child->rect.x, child->rect.y,
                child->rect.width, child->rect.height);
    }

    return layout_node;
}

LayoutDisplay LayoutTreeBuilder::display_for(DomNode const& node, StyleProperties const& style)
{
    // Document metadata is never painted: <head> and its contents carry the
    // title, styles and links, not visible text.
    if (node.type == DomNodeType::Element) {
        if (node.name == "head" || node.name == "title" || node.name == "meta"
            || node.name == "link" || node.name == "style" || node.name == "script"
            || node.name == "base" || node.name == "noscript")
            return LayoutDisplay::None;
    }
    if (auto value = style.get("display")) {
        if (*value == "none")
            return LayoutDisplay::None;
        if (*value == "inline")
            return LayoutDisplay::Inline;
        if (*value == "inline-block")
            return LayoutDisplay::InlineBlock;
        if (*value == "flex" || *value == "inline-flex")
            return LayoutDisplay::Flex;
        if (*value == "grid" || *value == "inline-grid")
            return LayoutDisplay::Grid;
    }
    if (node.type == DomNodeType::Text)
        return LayoutDisplay::Inline;
    return LayoutDisplay::Block;
}

void LayoutEngine::layout(LayoutNode& root, float viewport_width) const
{
    layout_node(root, 0, 0, viewport_width);
}

bool LayoutEngine::is_positioned(LayoutNode const& node)
{
    auto position = node.style.get("position");
    return position && (*position == "absolute" || *position == "fixed");
}

// Copies the geometry a previous pass recorded for this node and everything under it. The
// subtree is only entered when the node is clean and sits where it sat, so the recorded
// geometry is the geometry this pass would produce.
bool LayoutEngine::reuse_subtree(LayoutNode& node, float x, float y, float available_width) const
{
    if (!m_reuse || !m_reuse->geometry || !m_reuse->dirty)
        return false;
    if (!node.dom_node || m_reuse->dirty->count(node.dom_node))
        return false;
    auto const found = m_reuse->geometry->find(node.dom_node);
    if (found == m_reuse->geometry->end())
        return false;
    auto const& cached = found->second;
    if (cached.x != x || cached.y != y || cached.available_width != available_width)
        return false;

    std::function<void(LayoutNode&)> restore = [&](LayoutNode& current) {
        if (current.dom_node) {
            auto const entry = m_reuse->geometry->find(current.dom_node);
            if (entry != m_reuse->geometry->end()) {
                current.rect = entry->second.rect;
                current.box = entry->second.box;
                current.text_fragments = entry->second.fragments;
                current.layout_x = entry->second.x;
                current.layout_y = entry->second.y;
                current.layout_available_width = entry->second.available_width;
            }
        }
        for (auto& child : current.children)
            restore(*child);
    };
    restore(node);
    return true;
}

void LayoutEngine::layout_node(LayoutNode& node, float x, float y, float available_width) const
{
    if (node.display == LayoutDisplay::None) {
        node.rect = {};
        node.box = {};
        node.text_fragments.clear();
        return;
    }

    // The whole subtree can be taken from the last pass when nothing restyled inside it and it
    // is being laid out where it was. This is the per-node half of incremental layout: before
    // it, one changed box reflowed the document.
    if (reuse_subtree(node, x, y, available_width))
        return;

    if (m_reuse && m_reuse->laid_out)
        ++*m_reuse->laid_out;
    node.layout_x = x;
    node.layout_y = y;
    node.layout_available_width = available_width;

    node.box.margin = resolve_edges(node.style, "margin", available_width);
    node.box.padding = resolve_edges(node.style, "padding", available_width);
    node.box.border = resolve_edges(node.style, "border", available_width);

    auto specified_width = resolve_dimension(node.style, "width", available_width, -1);
    auto min_width = resolve_dimension(node.style, "min-width", available_width, 0);
    auto max_width = resolve_dimension(node.style, "max-width", available_width, -1);

    float horizontal_noncontent = node.box.margin.left + node.box.margin.right
        + node.box.padding.left + node.box.padding.right
        + node.box.border.left + node.box.border.right;

    float content_width = specified_width >= 0
        ? specified_width
        : std::max(0.0f, available_width - horizontal_noncontent);

    if (max_width >= 0)
        content_width = std::min(content_width, max_width);
    content_width = std::max(content_width, min_width);

    // "margin: 0 auto" centres a fixed-width block. The shorthand parser reports
    // auto as 0, so the auto-ness is re-read here from the raw values.
    if (specified_width >= 0) {
        auto is_auto = [&](char const* longhand, std::size_t index) -> bool {
            if (auto value = node.style.get(longhand))
                return *value == "auto";
            if (auto value = node.style.get("margin")) {
                std::vector<std::string> parts;
                std::string current;
                for (char c : *value) {
                    if (c == ' ' || c == '\t') {
                        if (!current.empty()) { parts.push_back(current); current.clear(); }
                    } else {
                        current.push_back(c);
                    }
                }
                if (!current.empty())
                    parts.push_back(current);
                if (!parts.empty() && parts.size() <= 4)
                    return parts[parts.size() == 1 ? 0 : index] == "auto";
            }
            return false;
        };
        bool left_auto = is_auto("margin-left", 3);
        bool right_auto = is_auto("margin-right", 1);
        if (left_auto || right_auto) {
            float used = node.box.padding.left + node.box.padding.right
                + node.box.border.left + node.box.border.right
                + (left_auto ? 0.0f : node.box.margin.left)
                + (right_auto ? 0.0f : node.box.margin.right);
            float free = available_width - content_width - used;
            if (free > 0.0f) {
                if (left_auto && right_auto) {
                    node.box.margin.left = free / 2.0f;
                    node.box.margin.right = free / 2.0f;
                } else if (left_auto) {
                    node.box.margin.left = free;
                } else {
                    node.box.margin.right = free;
                }
            }
        }
    }

    node.box.content.x = x + node.box.margin.left + node.box.border.left + node.box.padding.left;
    node.box.content.y = y + node.box.margin.top + node.box.border.top + node.box.padding.top;
    node.box.content.width = content_width;

    if (node.dom_node && node.dom_node->type == DomNodeType::Element && node.dom_node->name == "img") {
        float intrinsic_width = 0;
        float intrinsic_height = 0;
        if (auto value = node.dom_node->attribute("width")) intrinsic_width = parse_length(value, 0, available_width);
        if (auto value = node.dom_node->attribute("height")) intrinsic_height = parse_length(value, 0, available_width);
        if (specified_width < 0 && intrinsic_width > 0) { content_width = intrinsic_width; node.box.content.width = content_width; }
        float ratio = intrinsic_width > 0 && intrinsic_height > 0 ? intrinsic_height / intrinsic_width : 0.75f;
        float specified_height = parse_length(node.style.get("height"), -1, content_width);
        node.box.content.height = specified_height >= 0 ? specified_height : (intrinsic_height > 0 ? intrinsic_height : content_width * ratio);
    } else if (!node.generated_text.empty() || (node.dom_node && node.dom_node->type == DomNodeType::Text)) {
        std::string const& source = node.generated_text.empty() ? node.dom_node->data : node.generated_text;
        // A generated box is content-sized. A normal inline-block gets that from its parent's shrink-to-fit
        // pass, which measures the child's natural width; a generated box has no DOM node for that pass to
        // measure, so it kept the whole line and `::before { content: 'X'; display: inline-block }` painted a
        // full-width band (row 3a3). Shrink to the text's natural width, never growing past a specified one.
        if (node.is_generated && content_width > 0) {
            float const natural = TextLayout::measure_text(source, TextLayout::font_size(node.style), node.style);
            content_width = std::min(content_width, natural);
            node.box.content.width = content_width;
        }
        node.text_fragments = TextLayout::layout(source, node.box.content.x, node.box.content.y, content_width, node.style);
        float line_height = TextLayout::line_height(node.style);
        node.box.content.height = node.text_fragments.empty() ? 0
            : node.text_fragments.back().rect.y + line_height - node.box.content.y;
    } else {
        float cursor_y = node.box.content.y;
        bool has_inline = false;
        for (auto const& child : node.children) {
            if (!is_positioned(*child) && (child->display == LayoutDisplay::Inline || child->display == LayoutDisplay::InlineBlock))
                has_inline = true;
        }

        if (node.display == LayoutDisplay::Flex)
            layout_flex_children(node, content_width, cursor_y);
        else if (node.display == LayoutDisplay::Grid)
            layout_grid_children(node, content_width, cursor_y);
        else if (has_inline)
            layout_inline_children(node, node.box.content.x, cursor_y, content_width, cursor_y);
        else {
            for (auto& child : node.children) {
                if (child->display == LayoutDisplay::None || is_positioned(*child))
                    continue;
                layout_node(*child, node.box.content.x, cursor_y, content_width);
                cursor_y = child->rect.y + child->rect.height;
            }
        }

        float content_height = cursor_y - node.box.content.y;
        float specified_height = parse_length(node.style.get("height"), -1, content_width);
        float min_height = parse_length(node.style.get("min-height"), 0, content_width);
        float max_height = parse_length(node.style.get("max-height"), -1, content_width);

        node.box.content.height = specified_height >= 0 ? specified_height : content_height;
        if (auto ratio = node.style.get("aspect-ratio")) {
            auto slash = ratio->find('/');
            float rw = std::strtof(ratio->c_str(), nullptr);
            float rh = slash == std::string::npos ? 1.0f : std::strtof(ratio->c_str() + slash + 1, nullptr);
            if (rw > 0 && rh > 0 && specified_height < 0) node.box.content.height = content_width * rh / rw;
        }
        node.box.content.height = std::max(node.box.content.height, min_height);
        if (max_height >= 0)
            node.box.content.height = std::min(node.box.content.height, max_height);

        // An inline-level box is as wide as its content, not as wide as its container. Upstream gets
        // this from the inline formatting context, where an inline box's inline size is the extent of
        // what it placed (Rust/src/layout/inline_formatting_context.rs: `inline_size = glyph_end(last)
        // - origin`). Here the children were laid out against `content_width`, which for an inline box
        // was the whole remaining line - so every <span> filled the line and its siblings wrapped.
        //
        // Shrink-to-fit is the standard two-pass shape: the children are laid out against the
        // available width (already done above), and the box then takes the extent they produced.
        if (specified_width < 0
            && (node.display == LayoutDisplay::Inline || node.display == LayoutDisplay::InlineBlock)) {
            float extent = node.box.content.x;
            for (auto const& fragment : node.text_fragments)
                extent = std::max(extent, fragment.rect.x + fragment.rect.width);
            for (auto const& child : node.children) {
                if (child->display == LayoutDisplay::None || is_positioned(*child))
                    continue;
                extent = std::max(extent, child->rect.x + child->rect.width);
            }
            float const fitted = std::max(0.0f, extent - node.box.content.x);
            if (fitted > 0 && fitted < content_width) {
                content_width = fitted;
                node.box.content.width = fitted;
            }
        }

        layout_absolute_children(node, content_width);
    }

    node.rect.x = x + node.box.margin.left;
    node.rect.y = y + node.box.margin.top;
    node.rect.width = content_width + node.box.padding.left + node.box.padding.right + node.box.border.left + node.box.border.right;
    node.rect.height = node.box.content.height + node.box.padding.top + node.box.padding.bottom + node.box.border.top + node.box.border.bottom;

    auto position = node.style.get("position");
    if (position && *position == "relative") {
        node.rect.x += parse_length(node.style.get("left"), 0, available_width) - parse_length(node.style.get("right"), 0, available_width);
        node.rect.y += parse_length(node.style.get("top"), 0, available_width) - parse_length(node.style.get("bottom"), 0, available_width);
        node.box.content.x += parse_length(node.style.get("left"), 0, available_width) - parse_length(node.style.get("right"), 0, available_width);
        node.box.content.y += parse_length(node.style.get("top"), 0, available_width) - parse_length(node.style.get("bottom"), 0, available_width);
    }
}

void LayoutEngine::layout_inline_children(LayoutNode& node, float x, float y, float available_width, float& cursor_y) const
{
    float cursor_x = x;
    float line_height = TextLayout::line_height(node.style);
    float max_bottom = y;
    // A non-whitespace inline item may break only where whitespace precedes it. Upstream:
    // inline_formatting_context.rs, text_item_wrap_opportunity() - `line_ends_in_whitespace` or
    // `item.can_break_before`. Without it, two adjacent spans with no space between them wrap.
    bool line_ends_in_whitespace = false;

    for (auto& child : node.children) {
        if (child->display == LayoutDisplay::None || is_positioned(*child))
            continue;

        if (std::getenv("AETHERIS_TRACE_INLINE")) {
            std::string data = child->dom_node ? child->dom_node->data : std::string();
            std::fprintf(stderr, "TRACE child display=%d kind=%s data=[%s] cursor_x=%.1f width=%.1f avail=%.1f\n",
                static_cast<int>(child->display),
                child->dom_node ? (child->dom_node->type == DomNodeType::Text ? "text" : "element") : "none",
                data.substr(0, 12).c_str(), cursor_x, child->rect.width, available_width);
        }
        // A generated box flows like a text run: it is inline-level content, not a block that takes
        // the line. Both `::before` and `::after` used to take the block path, which put each on its
        // own line - which looked like `::after` not painting at all.
        bool const child_is_generated_text = child->is_generated;
        if (child->display == LayoutDisplay::Inline
            && (child_is_generated_text || (child->dom_node && child->dom_node->type == DomNodeType::Text))) {
            // A text run is laid out here rather than through layout_node, so it has to be counted
            // here too or `layout nodes:` under-reports what the pass did. It is not reusable:
            // every path that changes text replaces the text node with a new one, which has no
            // recorded geometry.
            if (m_reuse && m_reuse->laid_out)
                ++*m_reuse->laid_out;
            child->box.content.x = cursor_x;
            child->box.content.y = cursor_y;
            child->box.content.width = std::max(0.0f, x + available_width - cursor_x);
            child->text_fragments = TextLayout::layout(
                child_is_generated_text ? child->generated_text : child->dom_node->data,
                cursor_x, cursor_y, std::max(0.0f, x + available_width - cursor_x), child->style);
            if (!child->text_fragments.empty()) {
                for (auto const& fragment : child->text_fragments) {
                    if (fragment.rect.x < cursor_x && cursor_x > x)
                        cursor_y += line_height;
                    cursor_x = fragment.rect.x + fragment.rect.width;
                    cursor_y = fragment.rect.y;
                    max_bottom = std::max(max_bottom, fragment.rect.y + fragment.rect.height);
                }
                child->box.content.height = max_bottom - child->box.content.y;
                // The box is the text's extent, not the remaining line width. Giving it the whole
                // line made every inline ancestor's shrink-to-fit see a full-width child, so the
                // ancestor kept the line's width and its siblings wrapped.
                float text_extent = child->box.content.x;
                for (auto const& fragment : child->text_fragments)
                    text_extent = std::max(text_extent, fragment.rect.x + fragment.rect.width);
                child->box.content.width = std::max(0.0f, text_extent - child->box.content.x);
                child->rect = { child->box.content.x, child->box.content.y, child->box.content.width, child->box.content.height };
            }
            std::string const& text_source = child_is_generated_text ? child->generated_text : child->dom_node->data;
            line_ends_in_whitespace = !text_source.empty()
                && std::isspace(static_cast<unsigned char>(text_source.back()));
            continue;
        }

        // Only an inline-level child has a natural width to discover: it shrinks to fit. A block-level
        // child fills whatever line it is on, so its natural width *is* the available width and there
        // is nothing to measure - and measuring it laid its whole subtree out twice at every level,
        // which is what made deep nesting exponential (row 3k).
        bool const child_is_inline_level = child->display == LayoutDisplay::Inline
            || child->display == LayoutDisplay::InlineBlock;

        // A block-level child of an inline formatting context starts a new line at the container's left
        // edge - it is not placed at the inline cursor. Placing it there put a page's heading at the right
        // edge of its container (x = 24 + 976 on a 1024-wide page, so off the screen) and drew a block on
        // top of the text line before it (row 0.9).
        if (!child_is_inline_level) {
            if (cursor_x > x)
                cursor_y = std::max(cursor_y, max_bottom);
            layout_node(*child, x, cursor_y, available_width);
            cursor_x = x;
            max_bottom = child->rect.y + child->rect.height;
            cursor_y = max_bottom;
            line_ends_in_whitespace = false;
            continue;
        }

        float natural_width = available_width;
        if (child_is_inline_level) {
            // Measured against the whole line: an inline box shrinks to fit what it is given, so laying it
            // out against the space left clamps its width to exactly that and the fit test never fires.
            layout_node(*child, cursor_x, cursor_y, available_width);
            natural_width = child->rect.width;
        }
        bool const child_starts_with_whitespace = child->dom_node && child->dom_node->type == DomNodeType::Text
            && !child->dom_node->data.empty()
            && std::isspace(static_cast<unsigned char>(child->dom_node->data.front()));
        bool const may_break = line_ends_in_whitespace || child_starts_with_whitespace;
        if (std::getenv("AETHERIS_TRACE_INLINE"))
            std::fprintf(stderr, "TRACE decide natural=%.1f remaining=%.1f may_break=%d cursor_x=%.1f\n",
                natural_width, x + available_width - cursor_x, may_break ? 1 : 0, cursor_x);
        if (may_break && cursor_x > x && natural_width > x + available_width - cursor_x) {
            cursor_x = x;
            cursor_y = max_bottom;
            // The break moved the child, so its geometry has to be produced again at the new origin.
            layout_node(*child, cursor_x, cursor_y, available_width);
        } else if (natural_width > x + available_width - cursor_x) {
            // It fits nowhere on this line without shrinking: place it against what is left.
            layout_node(*child, cursor_x, cursor_y, x + available_width - cursor_x);
        }
        // Otherwise the measuring pass above already placed it: it was laid out at this origin with
        // a whole line to fill, it used less than that, and a shrink-to-fit box given more room than
        // it needs produces exactly the geometry it produced with the whole line. Laying it out a
        // second time to be told the same thing is what made deep nesting exponential (row 3k):
        // every inline box measured its subtree once and then laid it out again, so a document
        // nested d deep cost 2^d passes over the same nodes.
        cursor_x = child->rect.x + child->rect.width;
        max_bottom = std::max(max_bottom, child->rect.y + child->rect.height);
        line_ends_in_whitespace = false;
        if (cursor_x >= x + available_width) {
            cursor_x = x;
            cursor_y = max_bottom;
        }
    }

    cursor_y = std::max(max_bottom, cursor_y + line_height);
}

// The widest a node's content can be, used as the flex base size when an item names neither a basis nor a
// width. A block's max-content width is the widest of its children's; a text run's is the width its text
// measures at. It is an approximation of css-sizing-3's intrinsic width, and it is the part this engine was
// missing: without it an auto-basis flex item started at zero, so a row of text items vanished (row 0.10).
float LayoutEngine::max_content_width(LayoutNode const& node)
{
    if (auto const* width = node.style.get("width"); width && !width->empty() && *width != "auto") {
        float const specified = parse_length(width, -1, 0);
        if (specified >= 0)
            return specified;
    }

    float widest = 0;
    bool has_text = false;
    if (!node.generated_text.empty()) {
        widest = std::max(widest, TextLayout::measure_text(node.generated_text, TextLayout::font_size(node.style), node.style));
        has_text = true;
    } else if (node.dom_node && node.dom_node->type == DomNodeType::Text && !node.dom_node->data.empty()) {
        widest = std::max(widest, TextLayout::measure_text(node.dom_node->data, TextLayout::font_size(node.style), node.style));
        has_text = true;
    }
    if (has_text)
        return widest;

    // A block: the widest child, which is what "as wide as the widest thing inside it" means for a single
    // line. A row of inline children would be wider, so their widths are summed.
    float inline_sum = 0;
    bool has_inline = false;
    for (auto const& child : node.children) {
        if (child->display == LayoutDisplay::None)
            continue;
        float const child_width = max_content_width(*child);
        widest = std::max(widest, child_width);
        if (child->display == LayoutDisplay::Inline || child->display == LayoutDisplay::InlineBlock) {
            inline_sum += child_width;
            has_inline = true;
        }
    }
    return has_inline ? std::max(widest, inline_sum) : widest;
}

void LayoutEngine::layout_flex_children(LayoutNode& node, float available_width, float& cursor_y) const
{
    std::vector<LayoutNode*> items;
    for (auto& child : node.children) {
        if (child->display != LayoutDisplay::None && !is_positioned(*child))
            items.push_back(child.get());
    }
    if (items.empty())
        return;

    auto direction = node.style.get("flex-direction");
    bool column = direction && (*direction == "column" || *direction == "column-reverse");
    bool reverse = direction && (*direction == "row-reverse" || *direction == "column-reverse");
    if (reverse)
        std::reverse(items.begin(), items.end());

    float gap = parse_length(node.style.get("gap"), 0, available_width);
    if (column) {
        float cursor = node.box.content.y;
        float max_width = 0;
        for (auto* child : items) {
            layout_node(*child, node.box.content.x, cursor, available_width);
            cursor = child->rect.y + child->rect.height + gap;
            max_width = std::max(max_width, child->rect.width);
        }
        cursor_y = items.empty() ? cursor_y : cursor - gap;
        node.box.content.width = std::max(node.box.content.width, max_width);
        return;
    }

    // Flexbox §9.2-9.7, at the scale this engine can express: a flex base size per item, then grow or shrink
    // into the free space. The basis is `flex-basis` when it names a size, otherwise the item's `width`, and
    // `auto`/`content` fall back to zero - the engine has no intrinsic-width pass for a block child yet, which
    // is why an item with neither basis nor width starts at zero and grows from there (row 3c1).
    struct FlexItem {
        LayoutNode* node { nullptr };
        float base { 0 };
        float grow { 0 };
        float shrink { 1 }; // css-flexbox-1: flex-shrink's initial value is 1
        // Margin, padding and border, which sit outside the content box. The flex line is divided between
        // *outer* sizes: leaving them out put a padded item's content at the row's edge and its padding over
        // it - a 20px overflow at flex-end, and 22px on a real page's heading (row 0.10's tail).
        float extras { 0 };
        float margin_left { 0 };
    };
    std::vector<FlexItem> flex_items;
    flex_items.reserve(items.size());

    float total_base = gap * std::max(0, static_cast<int>(items.size()) - 1);
    float total_grow = 0;
    float total_scaled_shrink = 0;
    for (auto* child : items) {
        FlexItem item;
        item.node = child;
        item.grow = std::max(0.0f, parse_length(child->style.get("flex-grow"), 0, available_width));
        item.shrink = std::max(0.0f, parse_length(child->style.get("flex-shrink"), 1, available_width));

        auto const* basis = child->style.get("flex-basis");
        bool const basis_is_size = basis && !basis->empty() && *basis != "auto" && *basis != "content";
        if (basis_is_size)
            item.base = std::max(0.0f, parse_length(basis, 0, available_width));
        else if (auto const* width = child->style.get("width"); width && !width->empty() && *width != "auto")
            item.base = std::max(0.0f, parse_length(width, 0, available_width));
        else
            // `auto`/`content`: the item's own content decides, never more than the row it sits in.
            item.base = std::min(available_width, max_content_width(*child));

        {
            auto const margin = resolve_edges(child->style, "margin", available_width);
            auto const padding = resolve_edges(child->style, "padding", available_width);
            auto const border = resolve_edges(child->style, "border", available_width);
            item.margin_left = margin.left;
            item.extras = margin.left + margin.right + padding.left + padding.right + border.left + border.right;
        }

        total_base += item.base + item.extras;
        total_grow += item.grow;
        total_scaled_shrink += item.shrink * item.base;
        flex_items.push_back(item);
    }

    float const free_space = available_width - total_base;
    float cursor_x = node.box.content.x;
    float max_height = 0;
    auto justify = node.style.get("justify-content");
    float const positive_free = std::max(0.0f, free_space);
    float extra_gap = 0;
    if (justify && *justify == "center") cursor_x += positive_free / 2;
    else if (justify && *justify == "flex-end") cursor_x += positive_free;
    else if (justify && *justify == "space-between" && items.size() > 1) extra_gap = positive_free / (items.size() - 1);
    else if (justify && *justify == "space-around") { extra_gap = positive_free / items.size(); cursor_x += extra_gap / 2; }

    for (auto& item : flex_items) {
        float width = item.base;
        if (free_space > 0 && total_grow > 0) {
            width += free_space * item.grow / total_grow;
        } else if (free_space < 0 && total_scaled_shrink > 0) {
            // Shrinking is proportional to shrink factor * base size, so the widest item gives up the most.
            width += free_space * (item.shrink * item.base) / total_scaled_shrink;
            width = std::max(0.0f, width);
        }
        // `width` is the item's outer size, so the content box it is laid out with is what remains after the
        // margin, padding and border, and the box starts inside its left margin.
        float const content_width = std::max(0.0f, width - item.extras);
        layout_node(*item.node, cursor_x + item.margin_left, node.box.content.y, content_width);
        auto align = node.style.get("align-items");
        if (align && *align == "center") {
            item.node->rect.y += std::max(0.0f, max_height - item.node->rect.height) / 2;
        }
        cursor_x = item.node->rect.x + item.node->rect.width + gap + extra_gap;
        max_height = std::max(max_height, item.node->rect.height);
    }
    if (auto align = node.style.get("align-items"); align && *align == "flex-end") {
        for (auto* child : items) child->rect.y += max_height - child->rect.height;
    }
    cursor_y = node.box.content.y + max_height;
}

void LayoutEngine::layout_grid_children(LayoutNode& node, float available_width, float& cursor_y) const
{
    std::vector<LayoutNode*> items;
    for (auto& child : node.children)
        if (child->display != LayoutDisplay::None && !is_positioned(*child)) items.push_back(child.get());
    if (items.empty()) return;

    auto parse_tracks = [&](std::string const* value) {
        std::vector<float> tracks;
        if (!value) return tracks;
        std::string token; float fixed = 0;
        for (size_t i = 0; i <= value->size(); ++i) {
            char c = i < value->size() ? (*value)[i] : ' ';
            if (c == ' ' || c == '\t' || i == value->size()) {
                if (!token.empty()) {
                    if (token.size() >= 2 && token.substr(token.size()-2) == "fr") { tracks.push_back(-std::max(0.0f, std::strtof(token.c_str(), nullptr))); }
                    else { float v = parse_length(&token, 0, available_width); tracks.push_back(v); fixed += v; }
                    token.clear();
                }
            } else token += c;
        }
        float gap = parse_length(node.style.get("column-gap"), parse_length(node.style.get("gap"), 0, available_width), available_width);
        float remaining = std::max(0.0f, available_width - fixed - gap * std::max(0, (int)tracks.size()-1));
        float fr_total = 0; for (float v : tracks) if (v < 0) fr_total += -v;
        for (float& v : tracks) if (v < 0) v = fr_total > 0 ? remaining * (-v) / fr_total : 0;
        return tracks;
    };

    auto columns = parse_tracks(node.style.get("grid-template-columns"));

    // `grid-template-areas`: each quoted string is a row of names, `.` is an empty cell.
    auto split_tokens = [](std::string const& value) {
        std::vector<std::string> out;
        std::string token;
        for (char c : value) {
            if (c == ' ' || c == '\t' || c == '\n') {
                if (!token.empty()) { out.push_back(token); token.clear(); }
            } else {
                token += c;
            }
        }
        if (!token.empty())
            out.push_back(token);
        return out;
    };
    auto parse_areas = [&](std::string const* value) {
        std::vector<std::vector<std::string>> grid;
        if (!value)
            return grid;
        // The parser hands these over with single quotes around each row (the tokenizer's strings are
        // re-quoted on the way out), but a hand-written value may use double quotes; both are rows.
        std::size_t index = 0;
        while (index < value->size()) {
            if ((*value)[index] != '"' && (*value)[index] != '\'') { ++index; continue; }
            auto const quote = (*value)[index];
            auto const close = value->find(quote, index + 1);
            if (close == std::string::npos)
                break;
            std::vector<std::string> row;
            for (auto const& token : split_tokens(value->substr(index + 1, close - index - 1)))
                row.push_back(token == "." ? std::string() : token);
            grid.push_back(std::move(row));
            index = close + 1;
        }
        return grid;
    };
    auto const areas = parse_areas(node.style.get("grid-template-areas"));

    // A track count that can hold both the declared columns and the declared areas.
    std::size_t area_columns = 0;
    for (auto const& row : areas)
        area_columns = std::max(area_columns, row.size());
    if (columns.empty())
        columns.push_back(available_width);
    while (columns.size() < area_columns)
        columns.push_back(0); // an area column with no declared track: sized by its items below
    std::size_t const cols = columns.size();

    float gap_x = parse_length(node.style.get("column-gap"), parse_length(node.style.get("gap"), 0, available_width), available_width);
    float gap_y = parse_length(node.style.get("row-gap"), parse_length(node.style.get("gap"), 0, available_width), available_width);

    // Rows: `grid-template-rows` when it names pixel tracks, otherwise as tall as the tallest item in them.
    std::vector<float> row_heights;
    {
        auto const* declared = node.style.get("grid-template-rows");
        std::size_t index = 0;
        while (declared && index < declared->size()) {
            if ((*declared)[index] == ' ') { ++index; continue; }
            auto token_end = declared->find(' ', index);
            auto const token = declared->substr(index, token_end == std::string::npos ? std::string::npos : token_end - index);
            float const value = parse_length(&token, 0, node.box.content.height);
            row_heights.push_back(value);
            if (token_end == std::string::npos)
                break;
            index = token_end + 1;
        }
    }

    // Explicit placement first, then auto-placement into the first free cell - which is what the spec's
    // auto-flow does for items with no position, and it keeps a `grid-area` item from being pushed aside by
    // the ones before it (row 3c2).
    struct Placement {
        std::size_t row { 0 };
        std::size_t column { 0 };
        std::size_t row_span { 1 };
        std::size_t column_span { 1 };
    };
    std::vector<Placement> placements(items.size());
    std::vector<std::vector<bool>> occupied;

    auto ensure_rows = [&](std::size_t rows) {
        while (occupied.size() < rows)
            occupied.emplace_back(cols, false);
    };
    auto is_free = [&](Placement const& p) {
        ensure_rows(p.row + p.row_span);
        for (std::size_t r = p.row; r < p.row + p.row_span; ++r)
            for (std::size_t c = p.column; c < p.column + p.column_span && c < cols; ++c)
                if (occupied[r][c])
                    return false;
        return true;
    };
    auto take = [&](Placement const& p) {
        ensure_rows(p.row + p.row_span);
        for (std::size_t r = p.row; r < p.row + p.row_span; ++r)
            for (std::size_t c = p.column; c < p.column + p.column_span && c < cols; ++c)
                occupied[r][c] = true;
    };
    // `grid-column: 2` or `grid-column: 2 / 4` - 1-based, as the spec counts them. `span n` is not read.
    auto line_range = [&](std::string const* value, std::size_t& start, std::size_t& span) {
        if (!value || value->empty())
            return false;
        auto const slash = value->find('/');
        auto first = split_tokens(slash == std::string::npos ? *value : value->substr(0, slash));
        if (first.empty())
            return false;
        float const begin = std::strtof(first.front().c_str(), nullptr);
        if (begin <= 0)
            return false;
        start = static_cast<std::size_t>(begin) - 1;
        span = 1;
        if (slash != std::string::npos) {
            auto second = split_tokens(value->substr(slash + 1));
            if (!second.empty()) {
                float const end_line = std::strtof(second.front().c_str(), nullptr);
                if (end_line > begin)
                    span = static_cast<std::size_t>(end_line - begin);
            }
        }
        return true;
    };

    std::vector<bool> placed(items.size(), false);
    for (std::size_t index = 0; index < items.size(); ++index) {
        Placement p;
        bool explicit_placement = false;
        if (auto const* area = items[index]->style.get("grid-area"); area && !area->empty()) {
            // The name's bounding box in `grid-template-areas` is the item's cell.
            std::size_t min_row = SIZE_MAX, max_row = 0, min_column = SIZE_MAX, max_column = 0;
            for (std::size_t r = 0; r < areas.size(); ++r)
                for (std::size_t c = 0; c < areas[r].size(); ++c) {
                    if (areas[r][c] != *area)
                        continue;
                    min_row = std::min(min_row, r); max_row = std::max(max_row, r);
                    min_column = std::min(min_column, c); max_column = std::max(max_column, c);
                }
            if (min_row != SIZE_MAX) {
                p.row = min_row;
                p.column = min_column;
                p.row_span = max_row - min_row + 1;
                p.column_span = max_column - min_column + 1;
                explicit_placement = true;
            }
        }
        std::size_t column_start = 0, column_span = 1, row_start = 0, row_span = 1;
        bool const has_column = line_range(items[index]->style.get("grid-column"), column_start, column_span);
        bool const has_row = line_range(items[index]->style.get("grid-row"), row_start, row_span);
        if (!explicit_placement && (has_column || has_row)) {
            p.column = has_column ? column_start : 0;
            p.row = has_row ? row_start : 0;
            p.column_span = has_column ? column_span : 1;
            p.row_span = has_row ? row_span : 1;
            explicit_placement = true;
        }
        if (explicit_placement) {
            p.column = std::min(p.column, cols - 1);
            take(p);
            placements[index] = p;
            placed[index] = true;
        }
    }
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (placed[index])
            continue;
        Placement p;
        bool found = false;
        for (std::size_t row = 0; row < 1000 && !found; ++row) {
            ensure_rows(row + 1);
            for (std::size_t column = 0; column < cols; ++column) {
                Placement candidate { row, column, 1, 1 };
                if (is_free(candidate)) {
                    p = candidate;
                    found = true;
                    break;
                }
            }
        }
        take(p);
        placements[index] = p;
    }

    for (std::size_t index = 0; index < items.size(); ++index) {
        auto const& p = placements[index];
        while (row_heights.size() <= p.row + p.row_span - 1)
            row_heights.push_back(0);
        float x = node.box.content.x;
        for (std::size_t c = 0; c < p.column; ++c)
            x += columns[c] + gap_x;
        float width = gap_x * static_cast<float>(p.column_span - 1);
        for (std::size_t c = p.column; c < p.column + p.column_span && c < cols; ++c)
            width += columns[c];
        float y = node.box.content.y;
        for (std::size_t r = 0; r < p.row; ++r)
            y += row_heights[r] + gap_y;
        layout_node(*items[index], x, y, width);
        row_heights[p.row] = std::max(row_heights[p.row], items[index]->rect.height);
    }
    float total = 0;
    for (float h : row_heights) total += h;
    total += gap_y * std::max(0, static_cast<int>(row_heights.size()) - 1);
    cursor_y = node.box.content.y + total;
}

void LayoutEngine::layout_absolute_children(LayoutNode& node, float available_width) const
{
    for (auto& child : node.children) {
        auto position = child->style.get("position");
        if (!position || (*position != "absolute" && *position != "fixed"))
            continue;

        float child_width = resolve_dimension(child->style, "width", available_width, 0);
        float left = parse_length(child->style.get("left"), -1, available_width);
        float right = parse_length(child->style.get("right"), -1, available_width);
        if (child_width <= 0 && left >= 0 && right >= 0)
            child_width = std::max(0.0f, available_width - left - right);

        float base_x = *position == "fixed" ? 0 : node.box.content.x;
        float base_y = *position == "fixed" ? 0 : node.box.content.y;
        float child_x = left >= 0 ? base_x + left : (right >= 0 ? base_x + available_width - right - child_width : base_x);
        float child_y = base_y + parse_length(child->style.get("top"), 0, node.box.content.height);

        layout_node(*child, child_x, child_y, available_width);
    }
}

float LayoutEngine::parse_length(std::string const* value, float fallback, float percentage_base)
{
    if (!value || value->empty() || *value == "auto")
        return fallback;
    char* end = nullptr;
    auto parsed = std::strtof(value->c_str(), &end);
    if (end == value->c_str())
        return fallback;
    std::string unit(end);
    if (unit == "%")
        return percentage_base * parsed / 100.0f;
    // The engine only understood px and %; em and friends were read as bare
    // numbers, so "5em" became 5px. em/rem resolve against the root size.
    if (unit == "em" || unit == "rem")
        return parsed * 16.0f;
    if (unit == "pt")
        return parsed * 4.0f / 3.0f;
    if (unit == "pc")
        return parsed * 16.0f;
    if (unit == "in")
        return parsed * 96.0f;
    if (unit == "cm")
        return parsed * 96.0f / 2.54f;
    if (unit == "mm")
        return parsed * 96.0f / 25.4f;
    return parsed;
}

float LayoutEngine::resolve_dimension(StyleProperties const& style, char const* property, float available_width, float fallback)
{
    return parse_length(style.get(property), fallback, available_width);
}

BoxEdges LayoutEngine::resolve_edges(StyleProperties const& style, char const* prefix, float percentage_base)
{
    std::string base(prefix);

    // Shorthand first: "margin: 5em auto" and "padding: 2em" are used by nearly
    // every real page, and only the longhands were read, so they were ignored
    // entirely. One to four values map to top/right/bottom/left.
    float shorthand[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    bool have_shorthand[4] = { false, false, false, false };
    if (auto value = style.get(base)) {
        std::vector<std::string> parts;
        std::string current;
        for (char c : *value) {
            if (c == ' ' || c == '\t') {
                if (!current.empty()) { parts.push_back(current); current.clear(); }
            } else {
                current.push_back(c);
            }
        }
        if (!current.empty())
            parts.push_back(current);
        if (!parts.empty() && parts.size() <= 4) {
            auto pick = [&](std::size_t index) { return parts[parts.size() == 1 ? 0 : index]; };
            for (std::size_t index = 0; index < 4; ++index) {
                auto text = pick(index);
                if (text == "auto") {
                    have_shorthand[index] = true;
                    shorthand[index] = 0.0f; // resolved later by the block layout
                    continue;
                }
                have_shorthand[index] = true;
                shorthand[index] = parse_length(&text, 0, percentage_base);
            }
        }
    }

    auto side = [&](char const* name) {
        auto direct = base + "-" + name;
        if (auto value = style.get(direct))
            return parse_length(value, 0, percentage_base);
        std::size_t index = std::string(name) == "top" ? 0 : std::string(name) == "right" ? 1
            : std::string(name) == "bottom" ? 2 : 3;
        if (have_shorthand[index])
            return shorthand[index];
        if (base == "border") {
            auto width = std::string("border-") + name + "-width";
            if (auto value = style.get(width))
                return parse_length(value, 0, percentage_base);
        }
        return 0.0f;
    };
    return { side("top"), side("right"), side("bottom"), side("left") };
}

} // namespace aetheris::rendering

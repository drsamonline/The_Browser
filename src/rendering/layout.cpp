#include "layout.hpp"

#include "text_layout.hpp"

#include <algorithm>
#include <cstdlib>

namespace aetheris::rendering {

LayoutNode& LayoutNode::append_child(std::unique_ptr<LayoutNode> child)
{
    child->parent = this;
    children.push_back(std::move(child));
    return *children.back();
}

std::unique_ptr<LayoutNode> LayoutTreeBuilder::build(DomNode const& document, CssStyleSheet const& sheet) const
{
    return build_node(document, sheet, nullptr, nullptr);
}

std::unique_ptr<LayoutNode> LayoutTreeBuilder::build_node(DomNode const& node, CssStyleSheet const& sheet, LayoutNode* parent, StyleProperties const* parent_style) const
{
    auto layout_node = std::make_unique<LayoutNode>();
    layout_node->dom_node = &node;
    layout_node->parent = parent;
    layout_node->style = StyleResolver {}.resolve(node, sheet, parent_style);
    layout_node->display = display_for(node, layout_node->style);

    for (auto const& child : node.children)
        layout_node->append_child(build_node(*child, sheet, layout_node.get(), &layout_node->style));
    return layout_node;
}

LayoutDisplay LayoutTreeBuilder::display_for(DomNode const& node, StyleProperties const& style)
{
    if (auto value = style.get("display")) {
        if (*value == "none")
            return LayoutDisplay::None;
        if (*value == "inline")
            return LayoutDisplay::Inline;
    }
    if (node.type == DomNodeType::Text)
        return LayoutDisplay::Inline;
    return LayoutDisplay::Block;
}

void LayoutEngine::layout(LayoutNode& root, float viewport_width) const
{
    layout_node(root, 0, 0, viewport_width);
}

void LayoutEngine::layout_node(LayoutNode& node, float x, float y, float available_width)
{
    if (node.display == LayoutDisplay::None) {
        node.rect = {};
        node.box = {};
        node.text_fragments.clear();
        return;
    }

    node.box.margin = resolve_edges(node.style, "margin");
    node.box.padding = resolve_edges(node.style, "padding");
    node.box.border = resolve_edges(node.style, "border");

    float horizontal_noncontent = node.box.margin.left + node.box.margin.right
        + node.box.padding.left + node.box.padding.right
        + node.box.border.left + node.box.border.right;

    float specified_width = parse_length(node.style.get("width"), -1);
    float content_width = specified_width >= 0 ? specified_width : std::max(0.0f, available_width - horizontal_noncontent);

    node.box.content.x = x + node.box.margin.left + node.box.border.left + node.box.padding.left;
    node.box.content.y = y + node.box.margin.top + node.box.border.top + node.box.padding.top;
    node.box.content.width = content_width;

    if (node.dom_node && node.dom_node->type == DomNodeType::Text) {
        node.text_fragments = TextLayout::layout(node.dom_node->data, node.box.content.x, node.box.content.y, content_width, node.style);
        float line_height = TextLayout::line_height(node.style);
        node.box.content.height = node.text_fragments.empty() ? 0
            : node.text_fragments.back().rect.y + line_height - node.box.content.y;
    } else {
        float cursor_y = node.box.content.y;
        bool has_inline = false;
        for (auto const& child : node.children)
            has_inline = has_inline || child->display == LayoutDisplay::Inline;

        if (has_inline)
            layout_inline_children(node, node.box.content.x, cursor_y, content_width, cursor_y);
        else {
            for (auto& child : node.children) {
                if (child->display == LayoutDisplay::None)
                    continue;
                layout_node(*child, node.box.content.x, cursor_y, content_width);
                cursor_y = child->rect.y + child->rect.height;
            }
        }

        float content_height = cursor_y - node.box.content.y;
        float specified_height = parse_length(node.style.get("height"), -1);
        node.box.content.height = specified_height >= 0 ? specified_height : content_height;
    }

    node.rect.x = x + node.box.margin.left;
    node.rect.y = y + node.box.margin.top;
    node.rect.width = content_width + node.box.padding.left + node.box.padding.right + node.box.border.left + node.box.border.right;
    node.rect.height = node.box.content.height + node.box.padding.top + node.box.padding.bottom + node.box.border.top + node.box.border.bottom;
}

void LayoutEngine::layout_inline_children(LayoutNode& node, float x, float y, float available_width, float& cursor_y)
{
    float max_bottom = y;
    for (auto& child : node.children) {
        if (child->display == LayoutDisplay::None)
            continue;
        layout_node(*child, x, cursor_y, available_width);
        max_bottom = std::max(max_bottom, child->rect.y + child->rect.height);
    }
    cursor_y = max_bottom;
}

float LayoutEngine::parse_length(std::string const* value, float fallback)
{
    if (!value || value->empty() || *value == "auto")
        return fallback;
    char* end = nullptr;
    auto parsed = std::strtof(value->c_str(), &end);
    return end != value->c_str() ? parsed : fallback;
}

BoxEdges LayoutEngine::resolve_edges(StyleProperties const& style, char const* prefix)
{
    std::string base(prefix);
    auto side = [&](char const* name) {
        auto direct = base + "-" + name;
        if (auto value = style.get(direct))
            return parse_length(value, 0);
        if (base == "border") {
            auto width = std::string("border-") + name + "-width";
            if (auto value = style.get(width))
                return parse_length(value, 0);
        }
        return 0.0f;
    };
    return { side("top"), side("right"), side("bottom"), side("left") };
}

} // namespace aetheris::rendering

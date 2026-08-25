#include "layout.hpp"

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
    return build_node(document, sheet, nullptr);
}

std::unique_ptr<LayoutNode> LayoutTreeBuilder::build_node(DomNode const& node, CssStyleSheet const& sheet, LayoutNode* parent) const
{
    auto layout_node = std::make_unique<LayoutNode>();
    layout_node->dom_node = &node;
    layout_node->parent = parent;
    layout_node->style = StyleResolver {}.resolve(node, sheet);
    layout_node->display = display_for(node, layout_node->style);

    for (auto const& child : node.children) {
        if (auto child_layout = build_node(*child, sheet, layout_node.get()))
            layout_node->append_child(std::move(child_layout));
    }

    return layout_node;
}

LayoutDisplay LayoutTreeBuilder::display_for(DomNode const& node, StyleProperties const& style)
{
    if (auto value = style.get("display")) {
        if (*value == "none")
            return LayoutDisplay::None;
        if (*value == "inline")
            return LayoutDisplay::Inline;
        return LayoutDisplay::Block;
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
        return;
    }

    auto margin_top = resolve_vertical_spacing(node.style, "margin-top");
    auto margin_bottom = resolve_vertical_spacing(node.style, "margin-bottom");
    auto padding_top = resolve_vertical_spacing(node.style, "padding-top");
    auto padding_bottom = resolve_vertical_spacing(node.style, "padding-bottom");

    auto width = parse_length(node.style.get("width"), available_width);
    node.rect.x = x;
    node.rect.y = y + margin_top;
    node.rect.width = width;

    float cursor_y = node.rect.y + padding_top;
    float content_height = 0;

    for (auto& child : node.children) {
        if (child->display == LayoutDisplay::None)
            continue;

        layout_node(*child, x, cursor_y, width);
        cursor_y = child->rect.y + child->rect.height;
        content_height = cursor_y - (node.rect.y + padding_top);
    }

    auto explicit_height = node.style.get("height");
    node.rect.height = parse_length(explicit_height, content_height + padding_top + padding_bottom);
    if (!explicit_height)
        node.rect.height += margin_bottom;
}

float LayoutEngine::parse_length(std::string const* value, float fallback)
{
    if (!value || value->empty() || *value == "auto")
        return fallback;

    char* end = nullptr;
    auto parsed = std::strtof(value->c_str(), &end);
    return end != value->c_str() ? parsed : fallback;
}

float LayoutEngine::resolve_vertical_spacing(StyleProperties const& style, char const* property)
{
    return parse_length(style.get(property), 0);
}

} // namespace aetheris::rendering

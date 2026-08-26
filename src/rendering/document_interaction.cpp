#include "document_interaction.hpp"

#include <functional>

namespace aetheris::rendering {

bool DocumentInteraction::contains(LayoutRect const& rect, float x, float y)
{
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

LayoutNode const* DocumentInteraction::hit_test_node(LayoutNode const& node, float x, float y)
{
    for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
        if (auto const* hit = hit_test_node(**it, x, y))
            return hit;
    }
    return contains(node.rect, x, y) ? &node : nullptr;
}

DomNode const* DocumentInteraction::nearest_link(DomNode const* node)
{
    while (node) {
        if (node->type == DomNodeType::Element && node->name == "a" && node->attribute("href"))
            return node;
        node = node->parent;
    }
    return nullptr;
}

HitTestResult DocumentInteraction::hit_test(RenderDocument const& document, Viewport const& viewport, float viewport_x, float viewport_y) const
{
    auto point = viewport.to_document_coordinates(viewport_x, viewport_y);
    auto const* layout_node = hit_test_node(document.layout_root(), point.x, point.y);
    if (!layout_node)
        return {};

    HitTestResult result;
    result.layout_node = layout_node;
    result.dom_node = layout_node->dom_node;
    if (auto const* link = nearest_link(result.dom_node)) {
        auto const* href = link->attribute("href");
        if (href && !href->empty())
            result.link_url = Url::resolve(Url::parse(document.source_url()), *href);
    }
    return result;
}

LayoutNode const* DocumentInteraction::find_layout_node(LayoutNode const& node, DomNode const* dom)
{
    if (node.dom_node == dom)
        return &node;
    for (auto const& child : node.children) {
        if (auto const* found = find_layout_node(*child, dom))
            return found;
    }
    return nullptr;
}

std::optional<LayoutRect> DocumentInteraction::fragment_target(RenderDocument const& document, std::string const& fragment) const
{
    if (fragment.empty())
        return std::nullopt;

    std::function<DomNode const*(DomNode const&)> find = [&](DomNode const& node) -> DomNode const* {
        if (node.type == DomNodeType::Element) {
            if (auto const* id = node.attribute("id"); id && *id == fragment)
                return &node;
            if (auto const* name = node.attribute("name"); name && *name == fragment)
                return &node;
        }
        for (auto const& child : node.children) {
            if (auto const* found = find(*child))
                return found;
        }
        return nullptr;
    };

    auto const* target = find(document.document().root());
    if (!target)
        return std::nullopt;
    auto const* layout = find_layout_node(document.layout_root(), target);
    if (!layout)
        return std::nullopt;
    return layout->rect;
}

} // namespace aetheris::rendering

#pragma once

#include "render_document.hpp"
#include "url.hpp"
#include "viewport.hpp"

#include <optional>
#include <string>

namespace aetheris::rendering {

struct HitTestResult {
    LayoutNode const* layout_node { nullptr };
    DomNode const* dom_node { nullptr };
    std::optional<Url> link_url;

    bool hit() const { return layout_node != nullptr; }
    bool is_link() const { return link_url.has_value(); }
};

class DocumentInteraction {
public:
    HitTestResult hit_test(RenderDocument const&, Viewport const&, float viewport_x, float viewport_y) const;
    std::optional<LayoutRect> fragment_target(RenderDocument const&, std::string const& fragment) const;

private:
    static LayoutNode const* hit_test_node(LayoutNode const&, float document_x, float document_y);
    static LayoutNode const* find_layout_node(LayoutNode const&, DomNode const*);
    static DomNode const* nearest_link(DomNode const*);
    static bool contains(LayoutRect const&, float x, float y);
};

} // namespace aetheris::rendering

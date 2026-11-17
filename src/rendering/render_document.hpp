#pragma once

#include "document.hpp"
#include "layout.hpp"
#include "paint_executor.hpp"
#include "render_tree.hpp"
#include "resource.hpp"
#include "image.hpp"
#include "css_stylesheet.hpp"
#include "visual_state.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <string_view>
#include <vector>

namespace aetheris::rendering {

// The geometry one layout pass produced for one DOM node. Kept so a relayout that
// cannot change geometry can copy it instead of laying out again. Upstream keeps the
// same state on the layout box and marks boxes dirty; this is the per-node snapshot.
struct CachedGeometry {
    LayoutRect rect;
    BoxModel box;
    std::vector<TextFragment> fragments;
};

class RenderDocument {
public:
    static RenderDocument create(std::string_view html, std::string_view css, float viewport_width, ResourceCache* resources = nullptr, std::string_view source_url = {});
    // Builds from an already-parsed document, so a host can run scripts that
    // mutate the DOM before layout.
    static RenderDocument create_from_document(Document document, std::string_view css, float viewport_width, ResourceCache* resources = nullptr, std::string_view source_url = {});
    static std::optional<RenderDocument> create_from_resources(std::string_view document_url, std::string_view stylesheet_url, float viewport_width, ResourceCache& resources);
    static std::optional<RenderDocument> create_from_resources(std::string_view document_url, std::vector<std::string> const& stylesheet_urls, float viewport_width, ResourceCache& resources);

    ResourceCache& resources() { return m_resources; }
    ResourceCache const& resources() const { return m_resources; }
    std::string const& source_url() const { return m_source_url; }

    Document const& document() const { return m_document; }
    LayoutNode const& layout_root() const { return *m_layout_root; }
    // Size of the laid-out document, which is what the viewport scrolls within.
    LayoutRect document_rect() const { return m_layout_root->rect; }
    RenderTree const& render_tree() const { return m_render_tree; }

    SoftwareSurface render_to_surface(int width, int height, Color clear_color = { 0, 0, 0, 0 }) const;
    // Paints the viewport that starts at `offset` in document coordinates. The
    // document is not re-laid out: scrolling repaints, it does not reflow.
    SoftwareSurface render_to_surface(int width, int height, Color clear_color, PaintOffset offset) const;
    // Rebuilds layout and the render tree from the (possibly mutated) document.
    void relayout();
    // Rebuilds layout and the render tree for a new interaction state. Returns
    // false when the rebuild was skipped because no rule in the sheet mentions the
    // pseudo-class that changed - upstream's rule, cheaply applied.
    CssStyleSheet const& stylesheet() const { return m_stylesheet; }

    // What the last relayout cost in selector matching: how many nodes were resolved
    // and how many reused their cached style.
    StyleCacheStats const& last_style_stats() const { return m_last_style_stats; }

    // How many times the layout engine has actually run for this document, counting
    // the first layout. Reported beside the relayout count: a relayout that only
    // repainted shows up in one and not the other, which is how the skip is checked
    // from outside rather than taken on trust.
    std::size_t layout_passes() const { return m_layout_passes; }

    bool apply_visual_state(VisualInteractionState const& interaction_state, float viewport_width);
    std::size_t visual_generation() const { return m_visual_generation; }

private:
    Document m_document;
    std::unique_ptr<LayoutNode> m_layout_root;
    RenderTree m_render_tree;
    CssStyleSheet m_stylesheet;
    std::unordered_map<DomNode const*, StyleCacheEntry> m_style_cache;
    StyleCacheStats m_last_style_stats;
    std::unordered_map<DomNode const*, std::string> m_layout_keys;
    std::unordered_map<DomNode const*, CachedGeometry> m_geometry;
    std::size_t m_layout_passes { 0 };
    // The viewport width the recorded geometry was laid out at. Geometry is only
    // reusable at the width that produced it.
    float m_layout_width { 0 };
    float m_viewport_width { 0 };
    std::size_t m_visual_generation { 0 };
    // The state the last apply_visual_state() used. relayout() rebuilds layout
    // from scratch, and without this it rebuilds with no interaction state at
    // all - which silently discards :hover and :focus the moment anything calls
    // relayout(), as dispatch_click_at does right after a click focuses.
    std::optional<VisualInteractionState> m_last_visual_state;

    RenderDocument(Document document, std::unique_ptr<LayoutNode> layout_root, RenderTree render_tree, CssStyleSheet sheet);
    void resolve_images(LayoutNode&);

    // The ~40 properties that can change geometry, joined into one comparable string.
    // Two equal keys mean this node's box cannot move because of style.
    static std::string layout_key(StyleProperties const&);
    // True when any node's layout key differs from the one the last layout used.
    bool layout_keys_changed(LayoutNode const& root) const;
    // Records the geometry a layout pass just produced, and the keys it was produced
    // from, so the next relayout can tell whether anything layout-affecting moved.
    void snapshot_layout(LayoutNode const& root);
    // Copies the recorded geometry back onto a freshly built tree, node for node.
    void restore_geometry(LayoutNode& root) const;
    std::string resolve_resource_key(std::string const& reference) const;
    ResourceCache m_resources;
    ImageCache m_images;
    std::string m_source_url;
};

} // namespace aetheris::rendering

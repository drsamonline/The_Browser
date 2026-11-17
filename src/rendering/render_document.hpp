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
#include <unordered_set>
#include <string_view>
#include <vector>

namespace aetheris::rendering {

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

    // How many layout keys the last skip decision compared: the nodes whose style was
    // re-resolved, not the nodes in the document.
    std::size_t key_checks() const { return m_key_checks; }

    // How many nodes the layout engine actually laid out in the last pass. The rest of the
    // tree came from the geometry cache, so this is the cost of a pass rather than its size.
    std::size_t laid_out_nodes() const { return m_laid_out; }

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
    // How many layout keys the last skip decision compared. Reported as `key checks:` so the
    // cost of the decision is visible, not inferred.
    std::size_t m_key_checks { 0 };
    std::size_t m_laid_out { 0 };
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

    // The nodes whose style can change because a pseudo-class moved. Follows upstream's
    // ancestor_traversal_for_pseudo_class(): only `:focus-within` reaches ancestors, so
    // only a focus change has to invalidate the chain above the element. Returns false
    // when no element identifies what changed, and the cache cannot be trusted.
    bool collect_state_change_dirty(VisualInteractionState const&, std::unordered_set<DomNode const*>& dirty) const;
    // The ~40 properties that can change geometry, joined into one comparable string.
    // Two equal keys mean this node's box cannot move because of style.
    static std::string layout_key(StyleProperties const&);
    // True when any node whose style was re-resolved has a layout key different from the one
    // the last layout used. A node that reused its cached style kept the exact properties the
    // last snapshot digested, so it cannot differ and is not compared: the walk is over
    // `restyled`, not over the tree.
    bool layout_keys_changed(LayoutNode const& root, std::unordered_set<DomNode const*> const& restyled);
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

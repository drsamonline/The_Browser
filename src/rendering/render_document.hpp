#pragma once

#include "document.hpp"
#include "paint_executor.hpp"
#include "render_tree.hpp"
#include "resource.hpp"
#include "image.hpp"
#include "css_stylesheet.hpp"
#include "visual_state.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aetheris::rendering {

class RenderDocument {
public:
    static RenderDocument create(std::string_view html, std::string_view css, float viewport_width, ResourceCache* resources = nullptr);
    static std::optional<RenderDocument> create_from_resources(std::string_view document_url, std::string_view stylesheet_url, float viewport_width, ResourceCache& resources);
    static std::optional<RenderDocument> create_from_resources(std::string_view document_url, std::vector<std::string> const& stylesheet_urls, float viewport_width, ResourceCache& resources);

    ResourceCache& resources() { return m_resources; }
    ResourceCache const& resources() const { return m_resources; }
    std::string const& source_url() const { return m_source_url; }

    Document const& document() const { return m_document; }
    LayoutNode const& layout_root() const { return *m_layout_root; }
    RenderTree const& render_tree() const { return m_render_tree; }

    SoftwareSurface render_to_surface(int width, int height, Color clear_color = { 255, 255, 255, 255 }) const;
    void apply_visual_state(VisualInteractionState const& interaction_state, float viewport_width);
    std::size_t visual_generation() const { return m_visual_generation; }

private:
    Document m_document;
    std::unique_ptr<LayoutNode> m_layout_root;
    RenderTree m_render_tree;
    CssStyleSheet m_stylesheet;
    std::size_t m_visual_generation { 0 };

    RenderDocument(Document document, std::unique_ptr<LayoutNode> layout_root, RenderTree render_tree, CssStyleSheet sheet);
    void resolve_images(LayoutNode&);
    ResourceCache m_resources;
    ImageCache m_images;
    std::string m_source_url;
};

} // namespace aetheris::rendering

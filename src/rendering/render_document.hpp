#pragma once

#include "document.hpp"
#include "render_tree.hpp"

#include <string_view>

namespace aetheris::rendering {

class RenderDocument {
public:
    static RenderDocument create(std::string_view html, std::string_view css, float viewport_width);

    Document const& document() const { return m_document; }
    LayoutNode const& layout_root() const { return *m_layout_root; }
    RenderTree const& render_tree() const { return m_render_tree; }

private:
    Document m_document;
    std::unique_ptr<LayoutNode> m_layout_root;
    RenderTree m_render_tree;

    RenderDocument(Document document, std::unique_ptr<LayoutNode> layout_root, RenderTree render_tree);
};

} // namespace aetheris::rendering

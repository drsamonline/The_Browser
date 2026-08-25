#include "render_document.hpp"

#include "css_parser.hpp"
#include "layout.hpp"

namespace aetheris::rendering {

RenderDocument::RenderDocument(Document document, std::unique_ptr<LayoutNode> layout_root, RenderTree render_tree)
    : m_document(std::move(document))
    , m_layout_root(std::move(layout_root))
    , m_render_tree(std::move(render_tree))
{
}

RenderDocument RenderDocument::create(std::string_view html, std::string_view css, float viewport_width)
{
    auto document = Document::parse_html(html);
    auto sheet = CssParser {}.parse(css);

    auto layout_root = LayoutTreeBuilder {}.build(document.root(), sheet);
    LayoutEngine {}.layout(*layout_root, viewport_width);

    auto render_tree = RenderTree::from_layout(*layout_root);
    return RenderDocument(std::move(document), std::move(layout_root), std::move(render_tree));
}

} // namespace aetheris::rendering

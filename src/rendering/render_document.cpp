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

RenderDocument RenderDocument::create(std::string_view html, std::string_view css, float viewport_width, ResourceCache* resources)
{
    auto document = Document::parse_html(html);
    auto sheet = CssParser {}.parse(css);

    auto layout_root = LayoutTreeBuilder {}.build(document.root(), sheet);
    LayoutEngine {}.layout(*layout_root, viewport_width);

    auto render_tree = RenderTree::from_layout(*layout_root);
    RenderDocument result(std::move(document), std::move(layout_root), std::move(render_tree));
    if (resources) result.m_resources = *resources;
    result.resolve_images(*result.m_layout_root);
    result.m_render_tree = RenderTree::from_layout(*result.m_layout_root);
    return result;
}

std::optional<RenderDocument> RenderDocument::create_from_resources(std::string_view document_url, std::string_view stylesheet_url, float viewport_width, ResourceCache& resources)
{
    auto document = resources.get(std::string(document_url));
    auto stylesheet = resources.get(std::string(stylesheet_url));

    if (!document || !stylesheet)
        return std::nullopt;
    if (document->type != ResourceType::Document || stylesheet->type != ResourceType::Stylesheet)
        return std::nullopt;

    auto result = create(resources.get_text(document->url), resources.get_text(stylesheet->url), viewport_width, &resources);
    result.m_source_url = std::string(document_url);
    return result;
}

void RenderDocument::resolve_images(LayoutNode& node)
{
    if (node.dom_node && node.dom_node->type == DomNodeType::Element && node.dom_node->name == "img") { if (auto src=node.dom_node->attribute("src")) node.image=m_images.load(*src,m_resources); }
    if (auto background = node.style.get("background-image")) { auto value=*background; auto open=value.find("url("); auto close=value.rfind(')'); if(open!=std::string::npos && close!=std::string::npos && close>open+4) { auto url=value.substr(open+4,close-(open+4)); if(!url.empty() && (url.front()=='"' || url.front()=='\'')) url=url.substr(1,url.size()-2); node.background_image=m_images.load(url,m_resources); } }
    for (auto& child : node.children) resolve_images(*child);
}

SoftwareSurface RenderDocument::render_to_surface(int width, int height, Color clear_color) const
{
    SoftwareSurface surface(width, height, clear_color);
    PaintExecutor {}.execute(m_render_tree, surface);
    return surface;
}

} // namespace aetheris::rendering

#include "render_document.hpp"
#include <functional>
#include <unordered_set>

#include "css_parser.hpp"
#include "user_agent_stylesheet.hpp"
#include "dom_mutation.hpp"
#include "layout.hpp"
#include "url.hpp"

namespace aetheris::rendering {

RenderDocument::RenderDocument(Document document, std::unique_ptr<LayoutNode> layout_root, RenderTree render_tree, CssStyleSheet sheet)
    : m_document(std::move(document))
    , m_layout_root(std::move(layout_root))
    , m_render_tree(std::move(render_tree))
    , m_stylesheet(std::move(sheet))
{
}

namespace {

// The nodes whose style can change because `mutated` changed: the node, everything
// under it (a descendant selector may depend on it), and its ancestors (they inherit
// from their own parents and may match on their children's state). Siblings are safe
// for text and attribute writes - structural mutations clear the cache instead.
void collect_dirty(rendering::DomNode const& mutated, std::unordered_set<rendering::DomNode const*>& dirty)
{
    std::function<void(rendering::DomNode const&)> add_subtree = [&](rendering::DomNode const& node) {
        dirty.insert(&node);
        for (auto const& child : node.children)
            add_subtree(*child);
    };
    add_subtree(mutated);
    for (auto const* ancestor = mutated.parent; ancestor; ancestor = ancestor->parent)
        dirty.insert(ancestor);
}

// The properties that can move a box. Deliberately a hand-written list, not "every
// property except the paint ones": a property nobody thought of would be assumed
// paint-only and silently stop relaying out, which is a stale-frame bug. Anything
// missing here is a bug of that kind, so the list is the thing to extend.
char const* const kLayoutProperties[] = {
    "display", "width", "height", "min-width", "max-width", "min-height", "max-height",
    "margin", "margin-top", "margin-right", "margin-bottom", "margin-left",
    "padding", "padding-top", "padding-right", "padding-bottom", "padding-left",
    "border", "border-width", "border-top-width", "border-right-width",
    "border-bottom-width", "border-left-width",
    "font-size", "font-family", "font-weight", "line-height", "white-space",
    "position", "top", "right", "bottom", "left", "float", "overflow", "overflow-wrap",
    "text-align", "letter-spacing", "word-spacing",
};

} // namespace

std::string RenderDocument::layout_key(StyleProperties const& style)
{
    std::string key;
    for (auto const* property : kLayoutProperties) {
        auto const* value = style.get(property);
        if (!value)
            continue;
        key += property;
        key += '=';
        key += *value;
        key += ';';
    }
    return key;
}

bool RenderDocument::layout_keys_changed(LayoutNode const& root) const
{
    std::size_t seen = 0;
    std::function<bool(LayoutNode const&)> visit = [&](LayoutNode const& node) -> bool {
        if (node.dom_node) {
            ++seen;
            auto const found = m_layout_keys.find(node.dom_node);
            if (found == m_layout_keys.end() || found->second != layout_key(node.style))
                return true;
        }
        for (auto const& child : node.children)
            if (visit(*child))
                return true;
        return false;
    };
    if (visit(root))
        return true;
    // A node the last pass laid out and this tree does not have is a structural
    // change. relayout() already refuses to skip one; this is the belt to that brace.
    return seen != m_layout_keys.size();
}

void RenderDocument::snapshot_layout(LayoutNode const& root)
{
    m_layout_keys.clear();
    m_geometry.clear();
    std::function<void(LayoutNode const&)> visit = [&](LayoutNode const& node) {
        if (node.dom_node) {
            m_layout_keys[node.dom_node] = layout_key(node.style);
            CachedGeometry geometry;
            geometry.rect = node.rect;
            geometry.box = node.box;
            geometry.fragments = node.text_fragments;
            m_geometry[node.dom_node] = std::move(geometry);
        }
        for (auto const& child : node.children)
            visit(*child);
    };
    visit(root);
    m_layout_width = m_viewport_width;
}

void RenderDocument::restore_geometry(LayoutNode& root) const
{
    std::function<void(LayoutNode&)> visit = [&](LayoutNode& node) {
        if (node.dom_node) {
            auto const found = m_geometry.find(node.dom_node);
            if (found != m_geometry.end()) {
                node.rect = found->second.rect;
                node.box = found->second.box;
                node.text_fragments = found->second.fragments;
            }
        }
        for (auto& child : node.children)
            visit(*child);
    };
    visit(root);
}

RenderDocument RenderDocument::create(std::string_view html, std::string_view css, float viewport_width, ResourceCache* resources, std::string_view source_url)
{
    return create_from_document(Document::parse_html(html), css, viewport_width, resources, source_url);
}

RenderDocument RenderDocument::create_from_document(Document document, std::string_view css, float viewport_width, ResourceCache* resources, std::string_view source_url)
{
    // The UA stylesheet is the base layer: parsing it before the author CSS
    // means author rules of equal specificity win on source order, which is the
    // behaviour a page expects.
    std::string combined;
    combined.reserve(user_agent_stylesheet().size() + css.size() + 2);
    combined.append(user_agent_stylesheet());
    combined.push_back('\n');
    combined.append(css);
    auto sheet = CssParser {}.parse(combined);

    auto layout_root = LayoutTreeBuilder {}.build(document.root(), sheet);
    LayoutEngine {}.layout(*layout_root, viewport_width);

    auto render_tree = RenderTree::from_layout(*layout_root);
    RenderDocument result(std::move(document), std::move(layout_root), std::move(render_tree), std::move(sheet));
    result.m_viewport_width = viewport_width;
    if (resources) result.m_resources = *resources;
    result.m_source_url = std::string(source_url);
    result.resolve_images(*result.m_layout_root);
    result.m_render_tree = RenderTree::from_layout(*result.m_layout_root);
    // The first layout is a layout pass like any other, and it is the one the geometry
    // cache starts from.
    ++result.m_layout_passes;
    result.snapshot_layout(*result.m_layout_root);
    return result;
}

void RenderDocument::relayout()
{
    // Rebuild with the state the last apply_visual_state() used, or :hover and
    // :focus are silently discarded whenever anything calls relayout().
    std::unordered_set<DomNode const*> dirty;
    auto const* mutated = last_mutated_node();
    // A text write or a structural change can reflow, so the pass is never skipped for
    // one. An attribute write may only repaint; whether it did is decided below, by the
    // layout keys, not by the attribute's name.
    bool const text_or_structure = mutated == nullptr || last_attribute_mutation().empty();
    if (mutated) {
        collect_dirty(*mutated, dirty);
    } else {
        // A structural mutation: structure drives :first-child, :last-child and
        // sibling selectors, so nothing in the cache can be trusted.
        m_style_cache.clear();
    }
    m_last_style_stats = {};
    m_layout_root = LayoutTreeBuilder {}.build(m_document.root(), m_stylesheet,
        m_last_visual_state ? &*m_last_visual_state : nullptr,
        &m_style_cache, &dirty, &m_last_style_stats);

    // The skip: no box can have moved, so reuse the geometry the last pass produced
    // and rebuild only the render tree. resolve_images still runs, because an image
    // swap is a paint change and a skipped layout that also skipped the paint is the
    // stale-frame bug this project has shipped twice.
    if (!text_or_structure && !m_geometry.empty() && m_layout_width == m_viewport_width
        && !layout_keys_changed(*m_layout_root)) {
        restore_geometry(*m_layout_root);
        resolve_images(*m_layout_root);
        m_render_tree = RenderTree::from_layout(*m_layout_root);
        return;
    }

    ++m_layout_passes;
    LayoutEngine {}.layout(*m_layout_root, m_viewport_width);
    resolve_images(*m_layout_root);
    m_render_tree = RenderTree::from_layout(*m_layout_root);
    snapshot_layout(*m_layout_root);
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

std::optional<RenderDocument> RenderDocument::create_from_resources(std::string_view document_url, std::vector<std::string> const& stylesheet_urls, float viewport_width, ResourceCache& resources)
{
    auto document = resources.get(std::string(document_url));
    if (!document || document->type != ResourceType::Document) return std::nullopt;
    std::string css;
    for (auto const& stylesheet_url : stylesheet_urls) {
        auto stylesheet = resources.get(stylesheet_url);
        if (!stylesheet || stylesheet->type != ResourceType::Stylesheet) return std::nullopt;
        if (!css.empty()) css.push_back('\n');
        css += resources.get_text(stylesheet_url);
    }
    auto result = create(resources.get_text(document->url), css, viewport_width, &resources);
    result.m_source_url = std::string(document_url);
    return result;
}

std::string RenderDocument::resolve_resource_key(std::string const& reference) const
{
    // Resolve a raw href/src against the document URL so relative references
    // match the URL-keyed resource cache.
    if (m_source_url.empty() || reference.empty())
        return reference;
    auto resolved = Url::resolve(Url::parse(m_source_url), reference);
    return resolved.is_valid() ? resolved.serialized() : reference;
}

void RenderDocument::resolve_images(LayoutNode& node)
{
    if (node.dom_node && node.dom_node->type == DomNodeType::Element && node.dom_node->name == "img") { if (auto src=node.dom_node->attribute("src")) node.image=m_images.load(resolve_resource_key(*src),m_resources); }
    if (auto background = node.style.get("background-image")) { auto value=*background; auto open=value.find("url("); auto close=value.rfind(')'); if(open!=std::string::npos && close!=std::string::npos && close>open+4) { auto url=value.substr(open+4,close-(open+4)); if(!url.empty() && (url.front()=='"' || url.front()=='\'')) url=url.substr(1,url.size()-2); node.background_image=m_images.load(resolve_resource_key(url),m_resources); } }
    for (auto& child : node.children) resolve_images(*child);
}

bool RenderDocument::apply_visual_state(VisualInteractionState const& interaction_state, float viewport_width)
{
    if (m_visual_generation == interaction_state.generation && !interaction_state.layout_dirty && !interaction_state.paint_dirty)
        return false;

    // Upstream invalidates only the elements whose pseudo-class state changed. The
    // cheap half of that: if no selector mentions the class that just changed, no
    // element can restyle, so the rebuild is wasted work.
    std::string_view const pseudo = interaction_state.last_change == InteractionChange::Hover ? ":hover"
        : interaction_state.last_change == InteractionChange::Focus                          ? ":focus"
        : interaction_state.last_change == InteractionChange::Active                         ? ":active"
                                                                                            : "";
    if (!pseudo.empty() && !m_stylesheet.uses_pseudo_class(pseudo)) {
        m_visual_generation = interaction_state.generation;
        return false;
    }

    // Interaction state changes touch pseudo-classes everywhere, so the style cache
    // cannot be trusted: clear it rather than reason about which nodes are affected.
    m_style_cache.clear();
    m_last_visual_state = interaction_state;
    m_last_style_stats = {};
    m_layout_root = LayoutTreeBuilder {}.build(m_document.root(), m_stylesheet, &interaction_state, nullptr, nullptr, nullptr);
    // A state change rebuilds from scratch and does run the engine, so it counts as a
    // pass, and its geometry is the one a later skip has to reuse.
    ++m_layout_passes;
    LayoutEngine {}.layout(*m_layout_root, viewport_width);
    resolve_images(*m_layout_root);
    m_render_tree = RenderTree::from_layout(*m_layout_root);
    snapshot_layout(*m_layout_root);
    m_visual_generation = interaction_state.generation;
    return true;
}

SoftwareSurface RenderDocument::render_to_surface(int width, int height, Color clear_color) const
{
    return render_to_surface(width, height, clear_color, PaintOffset {});
}

SoftwareSurface RenderDocument::render_to_surface(int width, int height, Color clear_color, PaintOffset offset) const
{
    SoftwareSurface surface(width, height, clear_color);
    PaintExecutor {}.execute(m_render_tree, surface, offset);
    return surface;
}

} // namespace aetheris::rendering

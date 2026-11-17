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
void collect_dirty(rendering::DomNode const& mutated, std::unordered_set<rendering::DomNode const*>& dirty, bool include_ancestors = true)
{
    std::function<void(rendering::DomNode const&)> add_subtree = [&](rendering::DomNode const& node) {
        dirty.insert(&node);
        for (auto const& child : node.children)
            add_subtree(*child);
    };
    add_subtree(mutated);
    if (include_ancestors)
        for (auto const* ancestor = mutated.parent; ancestor; ancestor = ancestor->parent)
            dirty.insert(ancestor);
}

// The properties that cannot move a box, only repaint it. This is a deny-list on purpose.
//
// It used to be the other way round - a list of the properties that *do* affect layout - and
// that list was missing `gap`, `row-gap`, `column-gap`, `flex-direction`, `flex-grow`,
// `align-items`, `justify-content`, `grid-template-columns` and `aspect-ratio`, all of which
// `layout.cpp` reads. A class toggle that changed only `gap` therefore skipped the pass and
// left the boxes where they were: a stale frame, found by auditing this list against the
// properties the layout engine reads. With the deny-list, a property nobody classified costs a
// wasted layout pass instead, which is the failure this project can afford.
std::string_view const kPaintOnlyProperties[] = {
    "background-color", "background-image", "background-position", "background-repeat",
    "background-size", "border-color", "border-radius", "border-style", "box-shadow",
    "color", "object-fit", "opacity", "outline", "text-decoration", "transform", "z-index",
};

} // namespace

std::string RenderDocument::layout_key(StyleProperties const& style)
{
    // Everything the style resolved, minus what can only repaint. Shorthands never reach here -
    // `margin`, `padding`, `border` and `background` are expanded by the cascade - so only
    // longhands have to be named.
    return style.digest_excluding(kPaintOnlyProperties);
}

bool RenderDocument::layout_keys_changed(LayoutNode const& root, std::unordered_set<DomNode const*> const& restyled)
{
    m_key_checks = 0;
    // Nothing re-resolved, so no style moved and no key can have moved. This is the common
    // case for a relayout that was only worth doing because the DOM changed somewhere that
    // does not affect style, and it costs one comparison rather than one per node.
    if (restyled.empty())
        return false;

    std::size_t in_tree = 0;
    std::function<bool(LayoutNode const&)> visit = [&](LayoutNode const& node) -> bool {
        if (node.dom_node) {
            ++in_tree;
            if (restyled.count(node.dom_node)) {
                ++m_key_checks;
                auto const found = m_layout_keys.find(node.dom_node);
                if (found == m_layout_keys.end() || found->second != layout_key(node.style))
                    return true;
            }
        }
        for (auto const& child : node.children)
            if (visit(*child))
                return true;
        return false;
    };
    if (visit(root))
        return true;
    // A node the last pass laid out and this tree does not have is a structural change.
    // relayout() already refuses to skip one - a structural mutation clears the style cache,
    // so every node re-resolves and `restyled` is everything - and this is the belt to that
    // brace, at the cost of counting pointers rather than digesting styles.
    return in_tree != m_layout_keys.size();
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
            geometry.x = node.layout_x;
            geometry.y = node.layout_y;
            geometry.available_width = node.layout_available_width;
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

    // Resolve the first pass into the cache as well as the layout tree, so the first
    // interaction has something to reuse: without this the first :hover re-resolves the
    // whole page, and the cache only starts paying on the second.
    std::unordered_map<DomNode const*, StyleCacheEntry> cache;
    std::unordered_set<DomNode const*> const nothing_clean;
    StyleCacheStats stats;
    auto layout_root = LayoutTreeBuilder {}.build(document.root(), sheet, nullptr, &cache, &nothing_clean, &stats);
    // Counted like any other pass: the first one lays out every node, and the summary should say so.
    std::size_t laid_out = 0;
    LayoutReuse const counting { nullptr, nullptr, &laid_out };
    LayoutEngine { &counting }.layout(*layout_root, viewport_width);

    auto render_tree = RenderTree::from_layout(*layout_root);
    RenderDocument result(std::move(document), std::move(layout_root), std::move(render_tree), std::move(sheet));
    result.m_style_cache = std::move(cache);
    result.m_last_style_stats = stats;
    result.m_viewport_width = viewport_width;
    result.m_laid_out = laid_out;
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
    auto const& child_lists = last_child_list_mutations();
    // A text write or a structural change can reflow, so the pass is never skipped for
    // one. An attribute write may only repaint; whether it did is decided below, by the
    // layout keys, not by the attribute's name.
    bool const text_or_structure = mutated == nullptr || last_attribute_mutation().empty();
    if (!child_lists.empty()) {
        // A child list changed. The parent's subtree is what can see it - :first-child,
        // :last-child, :nth-child and :empty are all about the sibling sequence - and the chain
        // above it is what can inherit. Upstream records a tree delta for the departed or
        // arrived sequence and invalidates that; before this, any structural change cleared the
        // whole cache, so appending one span inside one row re-resolved the page.
        //
        // A positional selector is the only way one child's presence can restyle another. When the
        // sheet has none - the user-agent sheet has none, and most pages have none - only the
        // parent can change (`:empty`), and the new children resolve themselves because they have
        // no cache entry. That is the difference between appending a row to a 200-row page
        // re-resolving 406 nodes and re-resolving 4.
        bool const positional = m_stylesheet.uses_sibling_sequence();
        for (auto const* parent : child_lists) {
            if (positional) {
                collect_dirty(*parent, dirty);
            } else {
                dirty.insert(parent);
                for (auto const* ancestor = parent->parent; ancestor; ancestor = ancestor->parent)
                    dirty.insert(ancestor);
            }
        }
    } else if (mutated) {
        collect_dirty(*mutated, dirty);
    } else {
        // A structural mutation with no parent recorded: nothing in the cache can be trusted.
        m_style_cache.clear();
    }
    m_last_style_stats = {};
    std::unordered_set<DomNode const*> restyled;
    m_layout_root = LayoutTreeBuilder {}.build(m_document.root(), m_stylesheet,
        m_last_visual_state ? &*m_last_visual_state : nullptr,
        &m_style_cache, &dirty, &m_last_style_stats, &restyled);

    // The skip: no box can have moved, so reuse the geometry the last pass produced
    // and rebuild only the render tree. resolve_images still runs, because an image
    // swap is a paint change and a skipped layout that also skipped the paint is the
    // stale-frame bug this project has shipped twice.
    if (!text_or_structure && !m_geometry.empty() && m_layout_width == m_viewport_width
        && !layout_keys_changed(*m_layout_root, restyled)) {
        restore_geometry(*m_layout_root);
        resolve_images(*m_layout_root);
        m_render_tree = RenderTree::from_layout(*m_layout_root);
        // Nothing was laid out, so the counter says so rather than repeating the last pass.
        m_laid_out = 0;
        return;
    }

    ++m_layout_passes;
    // Per-node reuse: a node whose subtree nothing restyled and which sits where it sat takes
    // its geometry from the last pass. The dirty set is every restyled node and everything
    // above it - an ancestor's height can depend on a descendant's - so a clean node outside
    // it is untouched by this pass.
    std::unordered_set<DomNode const*> layout_dirty;
    for (auto const* node : restyled)
        for (auto const* current = node; current; current = current->parent)
            layout_dirty.insert(current);
    LayoutReuse const reuse { &m_geometry, &layout_dirty, &m_laid_out };
    m_laid_out = 0;
    LayoutEngine engine { &reuse };
    engine.layout(*m_layout_root, m_viewport_width);
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

bool RenderDocument::collect_state_change_dirty(VisualInteractionState const& interaction_state, std::unordered_set<DomNode const*>& dirty) const
{
    DomNode const* before = nullptr;
    DomNode const* after = nullptr;
    switch (interaction_state.last_change) {
    case InteractionChange::Hover:
        before = m_last_visual_state ? m_last_visual_state->hovered : nullptr;
        after = interaction_state.hovered;
        break;
    case InteractionChange::Active:
        before = m_last_visual_state ? m_last_visual_state->active : nullptr;
        after = interaction_state.active;
        break;
    case InteractionChange::Focus:
        before = m_last_visual_state ? m_last_visual_state->focused : nullptr;
        after = interaction_state.focused;
        break;
    case InteractionChange::None:
        return false;
    }
    if (before == after)
        return false;

    // A sibling combinator means the element whose state moved can restyle its own siblings:
    // `.a:hover + .b`. They are dirtied alongside it, and only when the sheet actually uses one.
    bool const sibling_sequence = m_stylesheet.uses_sibling_sequence();

    // Upstream's ancestor_traversal_for_pseudo_class(): `:focus-within` is the one
    // pseudo-class that propagates to ancestors. `:hover`, `:focus`, `:focus-visible` and
    // `:active` match the element itself, which is what style.cpp tests - read that, not
    // this comment, if it ever changes.
    //
    // The subtree is always dirty: `.card:hover .title` matches descendants, and they are
    // what goes stale when the ancestor's state moves. The ancestors are always dirty too,
    // because the element that *lost* the state may be an ancestor of the one that gained
    // it - hovering a card then its title has to restyle the card.
    bool const propagates = interaction_state.last_change == InteractionChange::Focus;
    for (auto const* node : { before, after }) {
        if (!node)
            continue;
        collect_dirty(*node, dirty, true);
        if (sibling_sequence && node->parent) {
            for (auto const& sibling : node->parent->children)
                if (sibling->type == DomNodeType::Element && sibling.get() != node)
                    dirty.insert(sibling.get());
        }
        if (!propagates)
            continue;
        // `:focus-within` on an ancestor styles its *other* descendants - `.card:focus-within
        // .title` matches a sibling of the focused input, which is neither on the chain nor in
        // the focused subtree - so an ancestor whose subtree a rule can reach that way is dirty
        // down to its leaves. Ask per ancestor, the way upstream does: the sheet-wide answer
        // let one `.card:focus-within .title` rule make every focus move coarse.
        //
        // The state to match with is the one in which the pseudo-class holds for this chain: the
        // new state for the element that gained focus, the old one for the element that lost it,
        // or the compound `#card:focus-within` would not match the card that just lost it.
        VisualInteractionState const* const state_for = node == after
            ? &interaction_state
            : (m_last_visual_state ? &*m_last_visual_state : nullptr);
        for (auto const* ancestor = node->parent; ancestor; ancestor = ancestor->parent)
            if (StyleResolver::matches_ancestor_pseudo_class_rule(*ancestor, m_stylesheet, ":focus-within", state_for))
                collect_dirty(*ancestor, dirty, false);
    }
    // Sibling combinators (`+`, `~`) are not matched by style.cpp, so a state change cannot
    // reach a sibling and none is dirtied. If they are ever added, this is the place.
    return !dirty.empty();
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
        // No rule can observe it, so nothing restyles - but the state did move, and the next
        // relayout() rebuilds with `m_last_visual_state`. Recording it here is what keeps
        // that rebuild from resurrecting a stale hover.
        m_last_visual_state = interaction_state;
        m_visual_generation = interaction_state.generation;
        return false;
    }

    // Only the elements whose state actually changed, not the whole document. When no
    // element identifies the change (a state that moved without the element moving, or a
    // change no pseudo-class covers) nothing in the cache can be trusted, so it goes.
    std::unordered_set<DomNode const*> dirty;
    bool const identified = collect_state_change_dirty(interaction_state, dirty);
    if (!identified)
        m_style_cache.clear();

    m_last_visual_state = interaction_state;
    m_last_style_stats = {};
    std::unordered_set<DomNode const*> restyled;
    m_layout_root = LayoutTreeBuilder {}.build(m_document.root(), m_stylesheet, &interaction_state,
        &m_style_cache, identified ? &dirty : nullptr, &m_last_style_stats, &restyled);

    // The same skip relayout() takes, for the same reason: a pseudo-class that only changes
    // a paint property must rebuild the render tree without moving any box.
    if (!m_geometry.empty() && m_layout_width == viewport_width && !layout_keys_changed(*m_layout_root, restyled)) {
        restore_geometry(*m_layout_root);
        resolve_images(*m_layout_root);
        m_render_tree = RenderTree::from_layout(*m_layout_root);
        m_laid_out = 0;
        m_visual_generation = interaction_state.generation;
        return true;
    }

    ++m_layout_passes;
    std::unordered_set<DomNode const*> layout_dirty;
    for (auto const* node : restyled)
        for (auto const* current = node; current; current = current->parent)
            layout_dirty.insert(current);
    LayoutReuse const reuse { &m_geometry, &layout_dirty, &m_laid_out };
    m_laid_out = 0;
    LayoutEngine engine { &reuse };
    engine.layout(*m_layout_root, viewport_width);
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

#pragma once

#include "css_stylesheet.hpp"
#include "dom.hpp"
#include "visual_state.hpp"

#include <span>
#include <string>
#include <string>
#include <string_view>
#include <unordered_map>

namespace aetheris::rendering {

class StyleProperties {
public:
    void set(std::string property, std::string value);
    std::string const* get(std::string const& property) const;
    bool contains(std::string const& property) const;
    void inherit_from(StyleProperties const& parent);

    // A stable digest of every property, used to decide whether a child's cached
    // style is still valid: if the parent's resolved style is unchanged, a clean
    // child resolves to the same thing it did last time.
    std::string fingerprint() const;

    // The same digest, without the named properties. Callers that want "everything except
    // these" say so with the exceptions rather than with an allow-list of their own: a
    // property nobody named is then included, which is the safe way to be wrong.
    std::string digest_excluding(std::span<std::string_view const> excluded) const;

private:
    std::unordered_map<std::string, std::string> m_properties;
};

// A node's resolved style, kept between relayouts so an unchanged subtree does not
// re-run selector matching. Upstream keeps this as computed-style state on the node;
// this is the same idea with an explicit cache.
struct StyleCacheEntry {
    std::string parent_fingerprint;
    StyleProperties properties;
};

struct StyleCacheStats {
    std::size_t resolves { 0 };
    std::size_t hits { 0 };
};


class StyleResolver {
public:
    StyleProperties resolve(DomNode const& node, CssStyleSheet const& sheet, StyleProperties const* parent_style = nullptr, VisualInteractionState const* interaction_state = nullptr) const;

    // Whether a rule puts `pseudo` on a compound that `node` matches and that is not the
    // last one - `.card:focus-within .title`. That is the only shape in which an element's
    // style can depend on an *ancestor* holding the pseudo-class, so it is the question to
    // ask before invalidating an ancestor's subtree. `interaction_state` must be the state in
    // which the pseudo-class holds for this element (the new one for an element gaining it,
    // the old one for an element losing it), or the compound will not match.
    static bool matches_ancestor_pseudo_class_rule(DomNode const& node, CssStyleSheet const& sheet,
        std::string_view pseudo, VisualInteractionState const* interaction_state);

private:
    static bool matches(DomNode const& node, std::string const& selector, VisualInteractionState const* interaction_state);
    static bool matches_simple(DomNode const& node, std::string const& selector, VisualInteractionState const* interaction_state);
    static int specificity(std::string const& selector);
    static void apply_declaration(StyleProperties& properties, CssDeclaration const& declaration);

    static void apply_initial_values(DomNode const& node, StyleProperties& properties);
};

} // namespace aetheris::rendering

#pragma once

#include "css_stylesheet.hpp"
#include "dom.hpp"
#include "visual_state.hpp"

#include <string>
#include <string>
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

private:
    static bool matches(DomNode const& node, std::string const& selector, VisualInteractionState const* interaction_state);
    static bool matches_simple(DomNode const& node, std::string const& selector, VisualInteractionState const* interaction_state);
    static int specificity(std::string const& selector);
    static void apply_declaration(StyleProperties& properties, CssDeclaration const& declaration);

    static void apply_initial_values(DomNode const& node, StyleProperties& properties);
};

} // namespace aetheris::rendering

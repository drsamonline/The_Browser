#pragma once

#include "css_stylesheet.hpp"
#include "dom.hpp"
#include "visual_state.hpp"

#include <string>
#include <unordered_map>

namespace aetheris::rendering {

class StyleProperties {
public:
    void set(std::string property, std::string value);
    std::string const* get(std::string const& property) const;
    bool contains(std::string const& property) const;
    void inherit_from(StyleProperties const& parent);

private:
    std::unordered_map<std::string, std::string> m_properties;
};

class StyleResolver {
public:
    StyleProperties resolve(DomNode const& node, CssStyleSheet const& sheet, StyleProperties const* parent_style = nullptr, VisualInteractionState const* interaction_state = nullptr) const;

private:
    static bool matches(DomNode const& node, std::string const& selector, VisualInteractionState const* interaction_state);
    static bool matches_simple(DomNode const& node, std::string const& selector, VisualInteractionState const* interaction_state);
    static int specificity(std::string const& selector);
    static void apply_declaration(StyleProperties& properties, CssDeclaration const& declaration);

    static bool is_inherited_property(std::string const& property);
    static void apply_initial_values(DomNode const& node, StyleProperties& properties);
};

} // namespace aetheris::rendering

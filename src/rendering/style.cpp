#include "style.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace aetheris::rendering {

void StyleProperties::set(std::string property, std::string value)
{
    m_properties[std::move(property)] = std::move(value);
}

std::string const* StyleProperties::get(std::string const& property) const
{
    auto it = m_properties.find(property);
    return it == m_properties.end() ? nullptr : &it->second;
}

bool StyleProperties::contains(std::string const& property) const
{
    return m_properties.contains(property);
}

void StyleProperties::inherit_from(StyleProperties const& parent)
{
    for (auto const* property : { "color", "font-family", "font-size", "font-weight", "line-height", "text-align", "text-decoration", "white-space", "letter-spacing", "word-spacing" }) {
        if (!contains(property)) {
            if (auto value = parent.get(property))
                set(property, *value);
        }
    }
}

StyleProperties StyleResolver::resolve(DomNode const& node, CssStyleSheet const& sheet, StyleProperties const* parent_style, VisualInteractionState const* interaction_state) const
{
    StyleProperties properties;

    struct Match {
        int specificity;
        size_t source_order;
        CssDeclaration const* declaration;
    };
    std::vector<Match> matching_declarations;

    for (auto const& rule : sheet.rules) {
        for (auto const& selector : rule.selectors) {
            if (!matches(node, selector, interaction_state))
                continue;
            for (auto const& declaration : rule.declarations)
                matching_declarations.push_back({ specificity(selector), rule.source_order, &declaration });
        }
    }

    std::stable_sort(matching_declarations.begin(), matching_declarations.end(), [](auto const& a, auto const& b) {
        if (a.specificity != b.specificity)
            return a.specificity < b.specificity;
        return a.source_order < b.source_order;
    });

    for (auto const& match : matching_declarations)
        apply_declaration(properties, *match.declaration);

    // Inheritance runs before initial values so inherited properties
    // (color, font, text-decoration, ...) reach descendants such as text
    // nodes; initial values then fill in only what is still unset.
    if (parent_style)
        properties.inherit_from(*parent_style);

    apply_initial_values(node, properties);

    return properties;
}

bool StyleResolver::matches(DomNode const& node, std::string const& selector, VisualInteractionState const* interaction_state)
{
    // Fast path: a selector with no combinators (the common case: "div",
    // ".note", "#app") is a single compound and needs no splitting/allocation.
    bool has_combinator = false;
    for (char c : selector) {
        if (c == '>' || std::isspace(static_cast<unsigned char>(c))) {
            has_combinator = true;
            break;
        }
    }
    if (!has_combinator)
        return matches_simple(node, selector, interaction_state);

    // Split the selector into compound selectors separated by combinators:
    // whitespace = descendant, '>' = child. Each compound (e.g. "p.note.featured")
    // contains no whitespace and is handled by matches_simple().
    struct Part {
        char combinator { ' ' }; // combinator linking the previous compound to this one
        std::string text;
    };
    std::vector<Part> parts;
    std::string current;
    char pending = ' ';
    bool have_part = false;

    auto flush = [&]() {
        if (have_part) {
            parts.push_back({ pending, current });
            current.clear();
            have_part = false;
        }
    };

    for (char c : selector) {
        if (c == '>') {
            flush();
            pending = '>';
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            flush();
            if (pending != '>')
                pending = ' ';
        } else {
            current += c;
            have_part = true;
        }
    }
    flush();

    if (parts.empty())
        return false;

    // Match the right-most compound against the node, then walk up the ancestor
    // chain for each remaining compound.
    if (!matches_simple(node, parts.back().text, interaction_state))
        return false;

    DomNode const* current_node = &node;
    for (size_t k = parts.size() - 1; k-- > 0;) {
        if (parts[k + 1].combinator == '>') {
            DomNode const* parent = current_node->parent;
            if (!parent || !matches_simple(*parent, parts[k].text, interaction_state))
                return false;
            current_node = parent;
        } else {
            DomNode const* ancestor = current_node->parent;
            while (ancestor && !matches_simple(*ancestor, parts[k].text, interaction_state))
                ancestor = ancestor->parent;
            if (!ancestor)
                return false;
            current_node = ancestor;
        }
    }
    return true;
}

bool StyleResolver::matches_simple(DomNode const& node, std::string const& selector, VisualInteractionState const* interaction_state)
{
    if (node.type != DomNodeType::Element)
        return false;

    // Callers pass a single compound selector (matches() splits on combinators),
    // so there is no whitespace to strip and no defensive copy is needed here.
    std::string const& text = selector;
    if (text.empty())
        return false;

    std::string tag;
    std::string id;
    std::vector<std::string> classes;
    std::vector<std::string> pseudo_classes;
    size_t i = 0;

    while (i < text.size() && text[i] != '#' && text[i] != '.' && text[i] != ':')
        tag += text[i++];

    while (i < text.size()) {
        char marker = text[i++];
        std::string value;
        while (i < text.size() && text[i] != '#' && text[i] != '.' && text[i] != ':')
            value += text[i++];
        if (marker == '#')
            id = value;
        else if (marker == '.' && !value.empty())
            classes.push_back(value);
        else if (marker == ':' && !value.empty())
            pseudo_classes.push_back(value);
    }

    if (!tag.empty() && tag != "*" && node.name != tag)
        return false;
    if (!id.empty()) {
        auto node_id = node.attribute("id");
        if (!node_id || *node_id != id)
            return false;
    }
    if (!classes.empty()) {
        auto node_classes = node.attribute("class");
        if (!node_classes)
            return false;
        std::istringstream stream(*node_classes);
        std::vector<std::string> available;
        std::string item;
        while (stream >> item)
            available.push_back(item);
        for (auto const& required : classes) {
            if (std::find(available.begin(), available.end(), required) == available.end())
                return false;
        }
    }
    for (auto const& pseudo : pseudo_classes) {
        if (!interaction_state) return false;
        if (pseudo == "hover" && interaction_state->hovered != &node) return false;
        if (pseudo == "focus" && interaction_state->focused != &node) return false;
        if (pseudo == "active" && interaction_state->active != &node) return false;
        if (pseudo != "hover" && pseudo != "focus" && pseudo != "active") return false;
    }
    return true;
}

int StyleResolver::specificity(std::string const& selector)
{
    auto is_ident_char = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
    };

    int ids = 0;
    int classes = 0;
    int elements = 0;

    size_t i = 0;
    while (i < selector.size()) {
        char c = selector[i];
        if (c == '#') {
            ++ids;
            ++i;
            while (i < selector.size() && is_ident_char(selector[i]))
                ++i;
        } else if (c == '.') {
            ++classes;
            ++i;
            while (i < selector.size() && is_ident_char(selector[i]))
                ++i;
        } else if (c == ':') {
            ++classes;
            ++i;
            if (i < selector.size() && selector[i] == ':')
                ++i; // pseudo-element marker; the name is counted once
            while (i < selector.size() && is_ident_char(selector[i]))
                ++i;
        } else if (std::isspace(static_cast<unsigned char>(c)) || c == '>' || c == '+' || c == '~' || c == '*') {
            ++i;
        } else {
            ++elements;
            ++i;
            while (i < selector.size() && is_ident_char(selector[i]))
                ++i;
        }
    }
    return ids * 10000 + classes * 100 + elements;
}

void StyleResolver::apply_declaration(StyleProperties& properties, CssDeclaration const& declaration)
{
    auto apply_four_sides = [&](std::string const& base) {
        std::istringstream stream(declaration.value);
        std::vector<std::string> values;
        std::string value;
        while (stream >> value)
            values.push_back(value);
        if (values.empty())
            return;
        auto top = values[0];
        auto right = values.size() > 1 ? values[1] : top;
        auto bottom = values.size() > 2 ? values[2] : top;
        auto left = values.size() > 3 ? values[3] : right;
        properties.set(base + "-top", top);
        properties.set(base + "-right", right);
        properties.set(base + "-bottom", bottom);
        properties.set(base + "-left", left);
    };

    if (declaration.property == "margin" || declaration.property == "padding") {
        apply_four_sides(declaration.property);
        return;
    }

    if (declaration.property == "border-width") {
        apply_four_sides("border");
        auto top = properties.get("border-top");
        auto right = properties.get("border-right");
        auto bottom = properties.get("border-bottom");
        auto left = properties.get("border-left");
        if (top) properties.set("border-top-width", *top);
        if (right) properties.set("border-right-width", *right);
        if (bottom) properties.set("border-bottom-width", *bottom);
        if (left) properties.set("border-left-width", *left);
        return;
    }

    if (declaration.property == "border-radius") {
        std::istringstream stream(declaration.value);
        std::string first;
        stream >> first;
        if (!first.empty())
            properties.set("border-radius", first);
        return;
    }

    properties.set(declaration.property, declaration.value);
}

void StyleResolver::apply_initial_values(DomNode const& node, StyleProperties& properties)
{
    // Only fills properties that are still unset, so values coming from the
    // cascade or inheritance are never clobbered by an initial value.
    auto set_default = [&](std::string const& key, std::string value) {
        if (!properties.contains(key))
            properties.set(key, std::move(value));
    };

    bool is_form_control = node.type == DomNodeType::Element
        && (node.name == "input" || node.name == "textarea" || node.name == "select" || node.name == "button");

    std::string default_display = "block";
    if (node.type == DomNodeType::Text)
        default_display = "inline";
    else if (is_form_control)
        default_display = "inline-block";
    set_default("display", default_display);

    set_default("color", "black");
    set_default("font-size", "16px");
    set_default("line-height", "normal");
    set_default("white-space", "normal");
    set_default("opacity", "1");
    set_default("position", "static");
    set_default("border-style", "solid");
    set_default("border-radius", "0");
    set_default("overflow", "visible");
    set_default("z-index", "auto");
    set_default("background-repeat", "repeat");
    set_default("background-position", "0 0");
    set_default("background-size", "auto");
    set_default("object-fit", "fill");

    if (is_form_control) {
        set_default("border-width", "1px");
        set_default("padding", "4px");
        set_default("background-color", "white");
    }
}

} // namespace aetheris::rendering

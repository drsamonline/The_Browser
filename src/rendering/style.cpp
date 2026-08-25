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
    for (auto const* property : { "color", "font-family", "font-size", "font-weight", "line-height", "text-align", "white-space", "letter-spacing", "word-spacing" }) {
        if (!contains(property)) {
            if (auto value = parent.get(property))
                set(property, *value);
        }
    }
}

StyleProperties StyleResolver::resolve(DomNode const& node, CssStyleSheet const& sheet, StyleProperties const* parent_style) const
{
    StyleProperties properties;
    apply_initial_values(node, properties);

    struct Match {
        int specificity;
        size_t source_order;
        CssDeclaration const* declaration;
    };
    std::vector<Match> matches;

    for (auto const& rule : sheet.rules) {
        for (auto const& selector : rule.selectors) {
            if (!matches(node, selector))
                continue;
            for (auto const& declaration : rule.declarations)
                matches.push_back({ specificity(selector), rule.source_order, &declaration });
        }
    }

    std::stable_sort(matches.begin(), matches.end(), [](auto const& a, auto const& b) {
        if (a.specificity != b.specificity)
            return a.specificity < b.specificity;
        return a.source_order < b.source_order;
    });

    for (auto const& match : matches)
        apply_declaration(properties, *match.declaration);

    if (parent_style)
        properties.inherit_from(*parent_style);

    return properties;
}

bool StyleResolver::matches(DomNode const& node, std::string const& selector)
{
    return matches_simple(node, selector);
}

bool StyleResolver::matches_simple(DomNode const& node, std::string const& selector)
{
    if (node.type != DomNodeType::Element)
        return false;

    auto text = selector;
    text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c); }), text.end());
    if (text.empty())
        return false;

    std::string tag;
    std::string id;
    std::vector<std::string> classes;
    size_t i = 0;

    while (i < text.size() && text[i] != '#' && text[i] != '.')
        tag += text[i++];

    while (i < text.size()) {
        char marker = text[i++];
        std::string value;
        while (i < text.size() && text[i] != '#' && text[i] != '.')
            value += text[i++];
        if (marker == '#')
            id = value;
        else if (marker == '.' && !value.empty())
            classes.push_back(value);
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
    return true;
}

int StyleResolver::specificity(std::string const& selector)
{
    int ids = 0;
    int classes = 0;
    int elements = 0;
    bool in_name = false;

    for (size_t i = 0; i < selector.size(); ++i) {
        auto c = selector[i];
        if (c == '#') {
            ++ids;
            in_name = false;
        } else if (c == '.') {
            ++classes;
            in_name = false;
        } else if (std::isalpha(static_cast<unsigned char>(c)) && !in_name) {
            ++elements;
            in_name = true;
        } else if (std::isspace(static_cast<unsigned char>(c)) || c == '>') {
            in_name = false;
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

bool StyleResolver::is_inherited_property(std::string const& property)
{
    return property == "color" || property == "font-family" || property == "font-size"
        || property == "font-weight" || property == "line-height" || property == "text-align"
        || property == "white-space" || property == "letter-spacing" || property == "word-spacing";
}

void StyleResolver::apply_initial_values(DomNode const& node, StyleProperties& properties)
{
    if (node.type == DomNodeType::Document)
        properties.set("display", "block");
    else if (node.type == DomNodeType::Text)
        properties.set("display", "inline");
    else
        properties.set("display", "block");

    properties.set("color", "black");
    properties.set("font-size", "16px");
    properties.set("line-height", "normal");
    properties.set("white-space", "normal");
    properties.set("opacity", "1");
    properties.set("position", "static");
    properties.set("border-style", "solid");
    properties.set("border-radius", "0");
    properties.set("overflow", "visible");
    properties.set("z-index", "auto");
    properties.set("background-repeat", "repeat");
    properties.set("background-position", "0 0");
    properties.set("background-size", "auto");
    properties.set("object-fit", "fill");
}

} // namespace aetheris::rendering

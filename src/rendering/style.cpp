#include "style.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace aetheris::rendering {

void StyleProperties::set(std::string property, std::string value)
{
    m_properties.insert_or_assign(std::move(property), std::move(value));
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
    for (auto const* property : { "color", "font-family", "font-size", "font-weight", "line-height", "text-align" }) {
        if (!contains(property)) {
            if (auto value = parent.get(property))
                set(property, *value);
        }
    }
}

namespace {

std::string trim_copy(std::string value)
{
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::vector<std::string> split_selector(std::string const& selector)
{
    std::vector<std::string> parts;
    std::string current;
    bool pending_space = false;

    auto flush = [&] {
        auto part = trim_copy(current);
        if (!part.empty())
            parts.push_back(std::move(part));
        current.clear();
    };

    for (char c : selector) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            flush();
            pending_space = !parts.empty();
            continue;
        }
        if (c == '>') {
            flush();
            if (!parts.empty())
                parts.push_back(">");
            pending_space = false;
            continue;
        }
        if (pending_space) {
            parts.push_back(" ");
            pending_space = false;
        }
        current += c;
    }
    flush();

    while (!parts.empty() && (parts.back() == " " || parts.back() == ">"))
        parts.pop_back();
    return parts;
}

}

StyleProperties StyleResolver::resolve(DomNode const& node, CssStyleSheet const& sheet, StyleProperties const* parent_style) const
{
    StyleProperties properties;
    apply_initial_values(node, properties);

    struct Candidate {
        int specificity;
        size_t order;
        std::string value;
    };
    std::unordered_map<std::string, Candidate> winners;

    for (auto const& rule : sheet.rules) {
        int best_specificity = -1;
        for (auto const& selector : rule.selectors) {
            if (matches(node, selector))
                best_specificity = std::max(best_specificity, specificity(selector));
        }
        if (best_specificity < 0)
            continue;

        for (auto const& declaration : rule.declarations) {
            auto it = winners.find(declaration.property);
            if (it == winners.end() || best_specificity > it->second.specificity
                || (best_specificity == it->second.specificity && rule.source_order >= it->second.order)) {
                winners.insert_or_assign(declaration.property, Candidate { best_specificity, rule.source_order, declaration.value });
            }
        }
    }

    for (auto const& [property, candidate] : winners)
        apply_declaration(properties, { property, candidate.value });

    if (parent_style)
        properties.inherit_from(*parent_style);
    return properties;
}

bool StyleResolver::matches(DomNode const& node, std::string const& selector)
{
    auto parts = split_selector(selector);
    if (parts.empty())
        return false;

    DomNode const* current = &node;
    int index = static_cast<int>(parts.size()) - 1;
    if (!matches_simple(*current, parts[index--]))
        return false;

    while (index >= 0) {
        auto combinator = " ";
        if (parts[index] == " " || parts[index] == ">")
            combinator = parts[index--];

        if (index < 0)
            return false;

        auto const& target = parts[index--];
        if (combinator == ">") {
            current = current->parent;
            if (!current || !matches_simple(*current, target))
                return false;
        } else {
            current = current->parent;
            while (current && !matches_simple(*current, target))
                current = current->parent;
            if (!current)
                return false;
        }
    }
    return true;
}

bool StyleResolver::matches_simple(DomNode const& node, std::string const& selector)
{
    if (node.type != DomNodeType::Element)
        return false;
    if (selector == "*")
        return true;

    std::string tag;
    std::string id;
    std::vector<std::string> classes;
    size_t i = 0;

    while (i < selector.size()) {
        if (selector[i] == '#') {
            ++i;
            size_t start = i;
            while (i < selector.size() && selector[i] != '#' && selector[i] != '.')
                ++i;
            id = selector.substr(start, i - start);
        } else if (selector[i] == '.') {
            ++i;
            size_t start = i;
            while (i < selector.size() && selector[i] != '#' && selector[i] != '.')
                ++i;
            classes.push_back(selector.substr(start, i - start));
        } else {
            size_t start = i;
            while (i < selector.size() && selector[i] != '#' && selector[i] != '.')
                ++i;
            tag = selector.substr(start, i - start);
        }
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
    if (declaration.property == "margin" || declaration.property == "padding") {
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
        properties.set(declaration.property + "-top", top);
        properties.set(declaration.property + "-right", right);
        properties.set(declaration.property + "-bottom", bottom);
        properties.set(declaration.property + "-left", left);
        return;
    }
    properties.set(declaration.property, declaration.value);
}

bool StyleResolver::is_inherited_property(std::string const& property)
{
    return property == "color" || property == "font-family" || property == "font-size"
        || property == "font-weight" || property == "line-height" || property == "text-align"
        || property == "white-space";
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
}

} // namespace aetheris::rendering

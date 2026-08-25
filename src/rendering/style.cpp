#include "style.hpp"

#include <algorithm>
#include <cctype>

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

StyleProperties StyleResolver::resolve(DomNode const& node, CssStyleSheet const& sheet) const
{
    StyleProperties properties;

    for (auto const& rule : sheet.rules) {
        bool matched = false;
        for (auto const& selector : rule.selectors) {
            if (matches(node, selector)) {
                matched = true;
                break;
            }
        }

        if (!matched)
            continue;

        for (auto const& declaration : rule.declarations)
            properties.set(declaration.property, declaration.value);
    }

    return properties;
}

bool StyleResolver::matches(DomNode const& node, std::string const& selector)
{
    if (node.type != DomNodeType::Element || selector.empty())
        return false;

    if (selector[0] == '#') {
        auto id = node.attribute("id");
        return id && *id == selector.substr(1);
    }

    if (selector[0] == '.') {
        auto classes = node.attribute("class");
        if (!classes)
            return false;

        auto target = selector.substr(1);
        size_t start = 0;
        while (start < classes->size()) {
            while (start < classes->size() && std::isspace(static_cast<unsigned char>((*classes)[start])))
                ++start;
            size_t end = start;
            while (end < classes->size() && !std::isspace(static_cast<unsigned char>((*classes)[end])))
                ++end;
            if (classes->substr(start, end - start) == target)
                return true;
            start = end;
        }
        return false;
    }

    return node.name == selector;
}

} // namespace aetheris::rendering

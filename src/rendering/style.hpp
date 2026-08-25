#pragma once

#include "css_stylesheet.hpp"
#include "dom.hpp"

#include <string>
#include <unordered_map>

namespace aetheris::rendering {

class StyleProperties {
public:
    void set(std::string property, std::string value);
    std::string const* get(std::string const& property) const;
    bool contains(std::string const& property) const;

private:
    std::unordered_map<std::string, std::string> m_properties;
};

class StyleResolver {
public:
    StyleProperties resolve(DomNode const& node, CssStyleSheet const& sheet) const;

private:
    static bool matches(DomNode const& node, std::string const& selector);
};

} // namespace aetheris::rendering

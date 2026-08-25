#pragma once

#include <string>
#include <vector>

namespace aetheris::rendering {

struct CssDeclaration {
    std::string property;
    std::string value;
};

struct CssRule {
    std::vector<std::string> selectors;
    std::vector<CssDeclaration> declarations;
};

struct CssStyleSheet {
    std::vector<CssRule> rules;

    bool empty() const { return rules.empty(); }
};

} // namespace aetheris::rendering

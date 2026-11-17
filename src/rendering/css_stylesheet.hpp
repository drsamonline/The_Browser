#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace aetheris::rendering {

struct CssDeclaration {
    std::string property;
    std::string value;
};

struct CssRule {
    std::vector<std::string> selectors;
    std::vector<CssDeclaration> declarations;
    size_t source_order { 0 };
};

struct CssStyleSheet {
    std::vector<CssRule> rules;
    bool empty() const { return rules.empty(); }

    // Whether any selector mentions `pseudo` (":hover", ":focus", ":active").
    // Upstream invalidates only the elements whose pseudo-class state changed; the
    // cheap half of that is not rebuilding at all when no rule could match.
    // Whether any selector is an attribute selector on `name` ("[data-n]"). Attribute
    // selectors are not supported by this parser yet, so this is always false today;
    // it exists so the caller asks the right question when they are.
    bool uses_attribute(std::string_view name) const
    {
        for (auto const& rule : rules)
            for (auto const& selector : rule.selectors) {
                std::string needle = "[";
                needle += name;
                if (selector.find(needle) != std::string::npos)
                    return true;
            }
        return false;
    }

    bool uses_pseudo_class(std::string_view pseudo) const
    {
        for (auto const& rule : rules)
            for (auto const& selector : rule.selectors)
                if (selector.find(pseudo) != std::string::npos)
                    return true;
        return false;
    }
};

} // namespace aetheris::rendering

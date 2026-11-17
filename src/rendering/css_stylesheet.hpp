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

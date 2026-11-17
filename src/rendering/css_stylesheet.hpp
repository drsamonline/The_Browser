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

    // Whether any selector puts `pseudo` on a compound that is not the last one, as in
    // `.card:focus-within .title`. Only then can an element's style depend on an *ancestor*
    // matching the pseudo-class, which is the one thing that makes a state change reach a
    // subtree other than the changed element's own. Upstream asks a sharper question - does
    // a rule mention this pseudo-class on *this* element - because it has selector analysis;
    // this is the sheet-wide version, and it answers "maybe" for the whole sheet.
    bool uses_pseudo_class_before_combinator(std::string_view pseudo) const
    {
        for (auto const& rule : rules)
            for (auto const& selector : rule.selectors) {
                for (auto at = selector.find(pseudo); at != std::string::npos;
                    at = selector.find(pseudo, at + pseudo.size())) {
                    // A combinator after the pseudo-class means its compound is an ancestor of
                    // the subject. `+` and `~` are not combinators in this engine, but treating
                    // them as boundaries only ever answers "maybe", which is the safe way to be
                    // wrong here.
                    if (selector.find_first_of(" >+~", at + pseudo.size()) != std::string::npos)
                        return true;
                }
            }
        return false;
    }
};

} // namespace aetheris::rendering

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

    // Whether any selector's subject can change when a *sibling sequence* changes: a positional
    // pseudo-class, or a sibling combinator. Both are ways one child's presence restyles another,
    // so this is the question the structural invalidation asks before narrowing to the parent alone
    // (render_document.cpp). `:not(` counts: its argument may be positional.
    //
    // It lives here, next to the sheet, so that adding a pseudo-class or combinator to style.cpp
    // and forgetting this list is one edit apart rather than two files apart.
    bool uses_sibling_sequence() const
    {
        static char const* const positional[] = {
            ":first-child", ":last-child", ":only-child", ":nth-child", ":nth-last-child",
            "-of-type", ":not(",
        };
        for (auto const& rule : rules)
            for (auto const& selector : rule.selectors) {
                for (auto const* pseudo : positional)
                    if (selector.find(pseudo) != std::string::npos)
                        return true;
                // A sibling combinator at the top level. Inside parentheses it belongs to a nested
                // selector (`:has(+ .x)`), which this engine answers false anyway.
                // Any depth, deliberately: `:has(+ .x)` is a sibling dependency written inside
                // parentheses, and skipping nested combinators would leave that page stale.
                for (char const c : selector)
                    if (c == '+' || c == '~')
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

    // A sheet-wide version of this question - does any selector put `pseudo` on a non-final
    // compound - lived here for one commit and was superseded by
    // `StyleResolver::matches_ancestor_pseudo_class_rule()`, which asks it of one element.
    // The sheet-wide answer let a single `.card:focus-within .title` make every focus move in
    // the document coarse; it is not kept as a convenience, because reaching for it again
    // would undo that.
};

} // namespace aetheris::rendering

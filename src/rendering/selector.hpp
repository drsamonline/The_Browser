#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace aetheris::rendering {

// One attribute selector: `[name]`, `[name=v]`, `[name~=v]`, with the optional ` i` / ` s` flag.
struct AttributeRequirement {
    enum class Operator { Present, Equals, Includes, DashMatch, Prefix, Suffix, Substring } op;
    std::string name;
    std::string value;
    // `[a=v i]`: the value compares ASCII case-insensitively.
    bool insensitive { false };
};

// One compound selector - `p.card.featured[data-x]` - parsed once, when the stylesheet is parsed.
struct CompoundSelector {
    std::string tag;
    std::string id;
    std::vector<std::string> classes;
    std::vector<std::string> pseudo_classes;
    std::vector<AttributeRequirement> attributes;
};

// One compound in a chain, with the combinator that links it to the compound on its left:
// ' ' descendant, '>' child, '+' next sibling, '~' subsequent sibling. The leftmost part's
// combinator is unused, which is the shape upstream gives it (LibWeb/CSS/Selector.h,
// Combinator::None on the subject's leftmost part).
struct SelectorPart {
    char combinator { ' ' };
    CompoundSelector compound;
};

// A whole selector, parsed once. Matching walks this structure and never re-reads selector text,
// which is upstream's shape: the parsed selector is an object (LibWeb/CSS/Selector.h) and matching
// evaluates it (LibWeb/Rust/src/css/dom_selector_matching.rs:1262, `matches()` on a prepared
// `selector.view()`). Specificity is computed here, once, instead of per node.
//
// Row 3j: before this, `StyleResolver::matches_simple` re-parsed the selector text and allocated
// five containers on every (node x rule) test, and `matches()` scanned for a combinator with an
// O(n^2) depth walk. Measured at ~10 us per (rule x node); see docs/COVERAGE-CORPUS.md.
struct CompiledSelector {
    std::vector<SelectorPart> parts;
    int specificity { 0 };
    std::string text;
    bool empty() const { return parts.empty(); }
};

// Parses a selector once. `text` is kept for diagnostics and for the paths that still match by
// text (`:has()`, `:is()` arguments, pseudo-element bases).
CompiledSelector compile_selector(std::string_view text);

// One compound selector, parsed. Exposed because the pseudo-class paths build compounds on the fly.
CompoundSelector parse_compound(std::string_view text);

// The cascade weight of a selector: ids * 10000 + classes * 100 + elements.
int selector_specificity(std::string const& selector);

// Splits a selector list on top-level commas, so `:is(.a, .b:not(.c, .d))` keeps its nesting.
std::vector<std::string> split_selector_list(std::string const& list);

} // namespace aetheris::rendering

#include "style.hpp"
#include "selector.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <sstream>
#include <vector>

namespace aetheris::rendering {

namespace {

// 1-based position of `node` among its element siblings, the sibling count, and the same two
// counted only among siblings with the same tag name - which is what the -of-type family means.
bool element_position(DomNode const& node, int& index, int& count, int& type_index, int& type_count)
{
    auto* parent = node.parent;
    if (!parent)
        return false;
    index = 0;
    count = 0;
    type_index = 0;
    type_count = 0;
    for (auto const& child : parent->children) {
        if (child->type != DomNodeType::Element)
            continue;
        ++count;
        if (child.get() == &node)
            index = count;
        if (child->name == node.name) {
            ++type_count;
            if (child.get() == &node)
                type_index = type_count;
        }
    }
    return index > 0;
}

// Substitutes `var(--name[, fallback])` in a declaration value. Upstream does this at resolve time
// against an inherited store, with a substitution context that detects a property referring back to
// itself (Rust/src/css/custom_properties.rs, resolve_vars); this walks the same three cases with a
// depth limit instead of a context.
//
// A reference that cannot be resolved is left as written, so the declaration is dropped by the parser
// rather than silently taking a wrong value - the same failure mode as an unparsed selector.
std::string substitute_vars(std::string const& value, StyleProperties const& available, int depth)
{
    if (depth > 8 || value.find("var(") == std::string::npos)
        return value;

    std::string out;
    std::size_t i = 0;
    while (i < value.size()) {
        auto const at = value.find("var(", i);
        if (at == std::string::npos) {
            out += value.substr(i);
            break;
        }
        out += value.substr(i, at - i);

        // Find the matching ')' so a fallback can itself contain var().
        std::size_t j = at + 4;
        int nest = 1;
        while (j < value.size() && nest > 0) {
            if (value[j] == '(')
                ++nest;
            else if (value[j] == ')')
                --nest;
            ++j;
        }
        if (nest > 0)
            return value; // unbalanced: leave the whole value alone

        std::string inner = value.substr(at + 4, j - 5);
        std::string name = inner;
        std::string fallback;
        bool have_fallback = false;
        auto comma = inner.find(',');
        if (comma != std::string::npos) {
            name = inner.substr(0, comma);
            fallback = inner.substr(comma + 1);
            have_fallback = true;
        }
        auto trim = [](std::string& text) {
            auto b = text.find_first_not_of(" \t\r\n");
            auto e = text.find_last_not_of(" \t\r\n");
            text = b == std::string::npos ? std::string() : text.substr(b, e - b + 1);
        };
        trim(name);
        trim(fallback);

        std::string replacement;
        if (auto const* found = available.get(name)) {
            replacement = *found;
        } else if (have_fallback) {
            replacement = fallback;
        } else {
            replacement = "var(" + inner + ")"; // unresolvable, left as written
        }
        out += substitute_vars(replacement, available, depth + 1);
        i = j;
    }
    return out;
}

// Splits a selector list on top-level commas, so `:is(.a, .b:not(.c, .d))` keeps its nesting.

// The An+B microsyntax, as far as `:nth-child()` and its siblings need it: `odd`, `even`, a plain
// integer, `n`, `-n`, `2n`, `2n+1`, `-n+3`, `+3n-2`. Returns false for anything else, so a page
// using a form this does not understand gets no match rather than a wrong one.
bool parse_an_plus_b(std::string argument, int& step, int& offset)
{
    // The argument arrives with whatever whitespace the author wrote; the microsyntax allows it
    // around the sign but not inside a number.
    std::string compact;
    for (char c : argument) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            compact += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (compact.empty())
        return false;
    if (compact == "odd") {
        step = 2;
        offset = 1;
        return true;
    }
    if (compact == "even") {
        step = 2;
        offset = 0;
        return true;
    }

    auto const n = compact.find('n');
    if (n == std::string::npos) {
        char* end = nullptr;
        long value = std::strtol(compact.c_str(), &end, 10);
        if (end == compact.c_str() || *end != '\0')
            return false;
        step = 0;
        offset = static_cast<int>(value);
        return true;
    }

    // `an` part.
    std::string const coefficient = compact.substr(0, n);
    if (coefficient.empty() || coefficient == "+") {
        step = 1;
    } else if (coefficient == "-") {
        step = -1;
    } else {
        char* end = nullptr;
        long value = std::strtol(coefficient.c_str(), &end, 10);
        if (end == coefficient.c_str() || *end != '\0')
            return false;
        step = static_cast<int>(value);
    }

    // `+b` part, which must be signed when present.
    std::string const remainder = compact.substr(n + 1);
    if (remainder.empty()) {
        offset = 0;
        return true;
    }
    if (remainder.front() != '+' && remainder.front() != '-')
        return false;
    char* end = nullptr;
    long value = std::strtol(remainder.c_str(), &end, 10);
    if (end == remainder.c_str() || *end != '\0')
        return false;
    offset = static_cast<int>(value);
    return true;
}

// Upstream's predicate (Rust/src/css/style/selector.rs, matches_an_plus_b): the position is
// step * k + offset for some k >= 0, which `difference / step >= 0` expresses for either sign.
bool matches_an_plus_b(int step, int offset, int position)
{
    if (step == 0)
        return position == offset;
    long const difference = static_cast<long>(position) - static_cast<long>(offset);
    return difference % step == 0 && difference / step >= 0;
}

bool matches_nth(std::string const& pseudo, std::string const& name, int position)
{
    auto const prefix = name + "(";
    if (pseudo.rfind(prefix, 0) != 0)
        return false;
    std::string argument = pseudo.substr(prefix.size());
    auto close = argument.rfind(')');
    if (close != std::string::npos)
        argument = argument.substr(0, close);
    int step = 0;
    int offset = 0;
    if (!parse_an_plus_b(argument, step, offset))
        return false;
    return matches_an_plus_b(step, offset, position);
}

bool matches_structural_pseudo(DomNode const& node, std::string const& pseudo)
{
    // `:empty` is about this node's children, not its position among siblings, so it is answered
    // before element_position(). Upstream's definition (DOM/SelectorQuery.cpp): "Child nodes and
    // their character data" - comments and doctypes do not count, and neither does text of length
    // zero.
    if (pseudo == "empty") {
        for (auto const& child : node.children) {
            if (child->type == DomNodeType::Text) {
                if (!child->data.empty())
                    return false;
            } else if (child->type == DomNodeType::Comment || child->type == DomNodeType::Doctype) {
                continue;
            } else {
                return false;
            }
        }
        return true;
    }

    // `:root` is the document's own element child, so it is answered before any sibling question.
    if (pseudo == "root")
        return node.parent && node.parent->type == DomNodeType::Document;

    int index = 0;
    int count = 0;
    int type_index = 0;
    int type_count = 0;
    if (!element_position(node, index, count, type_index, type_count))
        return false;
    if (pseudo == "first-child")
        return index == 1;
    if (pseudo == "last-child")
        return index == count;
    if (pseudo == "only-child")
        return count == 1;
    if (pseudo == "first-of-type")
        return type_index == 1;
    if (pseudo == "last-of-type")
        return type_index == type_count;
    if (pseudo == "only-of-type")
        return type_count == 1;
    if (matches_nth(pseudo, "nth-child", index))
        return true;
    if (matches_nth(pseudo, "nth-last-child", count - index + 1))
        return true;
    if (matches_nth(pseudo, "nth-of-type", type_index))
        return true;
    if (matches_nth(pseudo, "nth-last-of-type", type_count - type_index + 1))
        return true;
    if (pseudo.rfind("nth-", 0) == 0)
        return false;
    return false;
}

} // namespace



void StyleProperties::set(std::string property, std::string value)
{
    // Any change invalidates the cached digest (row 3j).
    m_fingerprint_valid = false;
    m_properties[std::move(property)] = std::move(value);
}

std::string StyleProperties::fingerprint() const
{
    // Computed once per style, not once per node that asks (row 3j).
    if (!m_fingerprint_valid) {
        m_fingerprint = digest_excluding({});
        m_fingerprint_valid = true;
    }
    return m_fingerprint;
}

void StyleProperties::for_each_property(std::function<void(std::string const&, std::string const&)> const& visit) const
{
    for (auto const& entry : m_properties)
        visit(entry.first, entry.second);
}

std::string StyleProperties::digest_excluding(std::span<std::string_view const> excluded) const
{
    std::vector<std::string> parts;
    parts.reserve(m_properties.size());
    for (auto const& entry : m_properties) {
        bool skip = false;
        for (auto const& name : excluded) {
            if (entry.first == name) {
                skip = true;
                break;
            }
        }
        if (skip)
            continue;
        parts.push_back(entry.first + ":" + entry.second);
    }
    // The map is unordered, so sort before joining or two equal styles compare unequal.
    std::sort(parts.begin(), parts.end());
    std::string joined;
    for (auto const& part : parts) {
        joined += part;
        joined.push_back('\x1f');
    }
    return joined;
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
    // `visibility` is inherited (CSS/Properties.json: "inherited": true), which is what lets a child
    // re-set `visible` inside a hidden ancestor. `direction` is inherited too (same file, "initial":
    // "ltr"), and it has to be here or a text node - which is where bidi reads it from - never sees the
    // block's `direction: rtl`.
    for (auto const* property : { "color", "direction", "font-family", "font-size", "font-weight", "line-height", "text-align", "text-decoration", "white-space", "letter-spacing", "word-spacing", "visibility" }) {
        if (!contains(property)) {
            if (auto value = parent.get(property))
                set(property, *value);
        }
    }
    // Custom properties inherit, which is the whole point of putting them on `:root`. Upstream keeps
    // them in a separate inheritance store for this (Rust/src/css/custom_properties.rs); here they are
    // ordinary entries in the same map, so they are copied by prefix.
    inherit_custom_properties(parent);
}

void StyleProperties::inherit_custom_properties(StyleProperties const& parent)
{
    parent.for_each_property([&](std::string const& name, std::string const& value) {
        if (name.rfind("--", 0) == 0 && !contains(name))
            set(name, value);
    });
}

StyleProperties StyleResolver::resolve_pseudo_element(DomNode const& node, CssStyleSheet const& sheet,
    std::string_view pseudo_element, StyleProperties const* parent_style,
    VisualInteractionState const* interaction_state) const
{
    StyleProperties properties;

    // A generated box inherits from the element it belongs to, not from the element's parent.
    if (parent_style)
        properties.inherit_from(*parent_style);

    std::string const suffix = "::" + std::string(pseudo_element);
    struct Match {
        int specificity;
        size_t source_order;
        CssDeclaration const* declaration;
    };
    std::vector<Match> matching;

    for (auto const& rule : sheet.rules) {
        for (auto const& selector : rule.selectors) {
            if (selector.size() <= suffix.size())
                continue;
            if (selector.compare(selector.size() - suffix.size(), suffix.size(), suffix) != 0)
                continue;
            std::string const base = selector.substr(0, selector.size() - suffix.size());
            if (!matches(node, base, interaction_state))
                continue;
            for (auto const& declaration : rule.declarations)
                matching.push_back({ specificity(base), rule.source_order, &declaration });
        }
    }

    std::stable_sort(matching.begin(), matching.end(), [](auto const& a, auto const& b) {
        if (a.specificity != b.specificity)
            return a.specificity < b.specificity;
        return a.source_order < b.source_order;
    });

    for (auto const& match : matching) {
        if (match.declaration->property.rfind("--", 0) == 0)
            apply_declaration(properties, *match.declaration);
    }
    for (auto const& match : matching) {
        if (match.declaration->property.rfind("--", 0) == 0)
            continue;
        if (match.declaration->value.find("var(") != std::string::npos) {
            CssDeclaration substituted { match.declaration->property,
                substitute_vars(match.declaration->value, properties, 0) };
            apply_declaration(properties, substituted);
        } else {
            apply_declaration(properties, *match.declaration);
        }
    }
    return properties;
}

StyleProperties StyleResolver::resolve(DomNode const& node, CssStyleSheet const& sheet, StyleProperties const* parent_style, VisualInteractionState const* interaction_state) const
{
    StyleProperties properties;

    // Inherited custom properties arrive before the cascade, because a declaration in this node's
    // rules may substitute one of them: `:root{--c:...}` is on an ancestor, and `div{background:
    // var(--c)}` is substituted while this node's declarations are applied. Full inheritance still
    // runs at the end, and finds these already set.
    if (parent_style)
        properties.inherit_custom_properties(*parent_style);

    struct Match {
        int specificity;
        size_t source_order;
        CssDeclaration const* declaration;
    };
    std::vector<Match> matching_declarations;

    for (auto const& rule : sheet.rules) {
        // The selectors were parsed once, when the stylesheet was parsed (row 3j). Matching walks
        // the parsed form and reads the specificity computed with it, instead of re-reading and
        // re-scoring the same selector text for every node on the page.
        //
        // A sheet built without the parser would carry text selectors only; those are compiled
        // here rather than silently matching nothing.
        std::vector<CompiledSelector> fallback;
        auto const* selectors = &rule.compiled;
        if (selectors->empty() && !rule.selectors.empty()) {
            fallback.reserve(rule.selectors.size());
            for (auto const& text : rule.selectors)
                fallback.push_back(compile_selector(text));
            selectors = &fallback;
        }
        for (auto const& selector : *selectors) {
            if (!matches(node, selector, interaction_state))
                continue;
            for (auto const& declaration : rule.declarations)
                matching_declarations.push_back({ selector.specificity, rule.source_order, &declaration });
        }
    }

    std::stable_sort(matching_declarations.begin(), matching_declarations.end(), [](auto const& a, auto const& b) {
        if (a.specificity != b.specificity)
            return a.specificity < b.specificity;
        return a.source_order < b.source_order;
    });

    // Custom properties are collected first, in their own pass. Upstream does the same, and the
    // reason is specificity: `:root{--c:...}` outranks `div{background:var(--c)}`, so in one pass the
    // div would be substituted before the variable it reads had been set.
    for (auto const& match : matching_declarations)
        if (match.declaration->property.rfind("--", 0) == 0)
            apply_declaration(properties, *match.declaration);

    for (auto const& match : matching_declarations) {
        if (match.declaration->property.rfind("--", 0) == 0)
            continue;
        if (match.declaration->value.find("var(") != std::string::npos) {
            CssDeclaration substituted { match.declaration->property,
                substitute_vars(match.declaration->value, properties, 0) };
            apply_declaration(properties, substituted);
        } else {
            apply_declaration(properties, *match.declaration);
        }
    }

    // The inline `style` attribute is applied last, so it wins over the
    // stylesheet (as an author-origin declaration with highest priority).
    if (node.type == DomNodeType::Element) {
        if (auto* inline_style = node.attribute("style")) {
            auto trim = [](std::string& text) {
                auto begin = text.find_first_not_of(" \t\r\n");
                auto end = text.find_last_not_of(" \t\r\n");
                text = begin == std::string::npos ? std::string() : text.substr(begin, end - begin + 1);
            };
            std::istringstream declarations(*inline_style);
            std::string declaration;
            while (std::getline(declarations, declaration, ';')) {
                auto colon = declaration.find(':');
                if (colon == std::string::npos)
                    continue;
                std::string property = declaration.substr(0, colon);
                std::string value = declaration.substr(colon + 1);
                trim(property);
                trim(value);
                if (property.empty() || value.empty())
                    continue;
                CssDeclaration parsed { property, value };
                if (parsed.property.rfind("--", 0) == 0 || parsed.value.find("var(") == std::string::npos) {
                    apply_declaration(properties, parsed);
                } else {
                    CssDeclaration substituted { parsed.property, substitute_vars(parsed.value, properties, 0) };
                    apply_declaration(properties, substituted);
                }
            }
        }
    }

    // Inheritance runs before initial values so inherited properties
    // (color, font, text-decoration, ...) reach descendants such as text
    // nodes; initial values then fill in only what is still unset.
    if (parent_style)
        properties.inherit_from(*parent_style);

    apply_initial_values(node, properties);

    return properties;
}

// Matches by selector text. The text is compiled first; the recursive paths that still work in
// text (`:has()`, `:is()`/`:where()`/`:not()` arguments) come through here.
bool StyleResolver::matches(DomNode const& node, std::string const& selector, VisualInteractionState const* interaction_state)
{
    return matches(node, compile_selector(selector), interaction_state);
}

// Matches a selector that was parsed once, when the stylesheet was parsed (row 3j). This is the
// old `matches()` body with the text scan lifted out: it walks `compiled.parts` instead of
// splitting the selector string, and reads each compound's parsed form instead of re-parsing it.
bool StyleResolver::matches(DomNode const& node, CompiledSelector const& compiled, VisualInteractionState const* interaction_state)
{
    auto const& parts = compiled.parts;

    if (parts.empty())
        return false;

    // Match the right-most compound against the node, then walk up the ancestor
    // chain for each remaining compound.
    if (!matches_compound(node, parts.back().compound, interaction_state))
        return false;

    // The previous element sibling, which is what `+` and `~` step through. Upstream walks
    // right-to-left and asks tree.previous_sibling() for `+` and all earlier siblings for `~`.
    auto previous_element_sibling = [](DomNode const& from) -> DomNode const* {
        auto const* parent = from.parent;
        if (!parent)
            return nullptr;
        DomNode const* previous = nullptr;
        for (auto const& child : parent->children) {
            if (child.get() == &from)
                return previous;
            if (child->type == DomNodeType::Element)
                previous = child.get();
        }
        return nullptr;
    };

    DomNode const* current_node = &node;
    for (size_t k = parts.size() - 1; k-- > 0;) {
        char const combinator = parts[k + 1].combinator;
        if (combinator == '>') {
            DomNode const* parent = current_node->parent;
            if (!parent || !matches_compound(*parent, parts[k].compound, interaction_state))
                return false;
            current_node = parent;
        } else if (combinator == '+') {
            DomNode const* sibling = previous_element_sibling(*current_node);
            if (!sibling || !matches_compound(*sibling, parts[k].compound, interaction_state))
                return false;
            current_node = sibling;
        } else if (combinator == '~') {
            DomNode const* sibling = previous_element_sibling(*current_node);
            while (sibling && !matches_compound(*sibling, parts[k].compound, interaction_state))
                sibling = previous_element_sibling(*sibling);
            if (!sibling)
                return false;
            current_node = sibling;
        } else {
            DomNode const* ancestor = current_node->parent;
            while (ancestor && !matches_compound(*ancestor, parts[k].compound, interaction_state))
                ancestor = ancestor->parent;
            if (!ancestor)
                return false;
            current_node = ancestor;
        }
    }
    return true;
}

bool StyleResolver::matches_ancestor_pseudo_class_rule(DomNode const& node, CssStyleSheet const& sheet,
    std::string_view pseudo, VisualInteractionState const* interaction_state)
{
    // The combinators this engine matches: descendant (whitespace) and child ('>'). '+' and
    // '~' are not combinators here, so treating them as boundaries can only over-answer.
    static constexpr std::string_view combinators { " >+~" };
    for (auto const& rule : sheet.rules) {
        for (auto const& selector : rule.selectors) {
            for (auto at = selector.find(pseudo); at != std::string::npos;
                at = selector.find(pseudo, at + pseudo.size())) {
                // A combinator after the pseudo-class means its compound is an ancestor of the
                // subject. Without one, the pseudo-class is on the subject itself and the
                // element's own style is the only thing it changes.
                auto const end = selector.find_first_of(combinators, at + pseudo.size());
                if (end == std::string::npos)
                    continue;
                auto const previous = at == 0 ? std::string::npos : selector.find_last_of(combinators, at - 1);
                auto const begin = previous == std::string::npos ? 0 : previous + 1;
                if (matches_compound(node, parse_compound(selector.substr(begin, end - begin)), interaction_state))
                    return true;
            }
        }
    }
    return false;
}

// Whether `node` matches one parsed compound selector. The parse phase of the old
// `matches_simple` now runs once per selector (`parse_compound`), not once per (node x rule);
// what is left here is the part that actually depends on the node.
bool StyleResolver::matches_compound(DomNode const& node, CompoundSelector const& compound, VisualInteractionState const* interaction_state)
{
    if (node.type != DomNodeType::Element)
        return false;

    auto const& tag = compound.tag;
    auto const& id = compound.id;
    auto const& classes = compound.classes;
    auto const& pseudo_classes = compound.pseudo_classes;
    auto const& attributes = compound.attributes;

    for (auto const& requirement : attributes) {
        auto present = node.attribute(requirement.name);
        if (!present)
            return false;
        auto const& actual_raw = *present;
        auto const& expected_raw = requirement.value;
        std::string actual_lowered;
        std::string expected_lowered;
        std::string const* actual_ptr = &actual_raw;
        std::string const* expected_ptr = &expected_raw;
        if (requirement.insensitive) {
            auto lower = [](std::string const& text) {
                std::string out = text;
                for (auto& c : out)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return out;
            };
            actual_lowered = lower(actual_raw);
            expected_lowered = lower(expected_raw);
            actual_ptr = &actual_lowered;
            expected_ptr = &expected_lowered;
        }
        auto const& actual = *actual_ptr;
        auto const& expected = *expected_ptr;
        switch (requirement.op) {
        case AttributeRequirement::Operator::Present:
            break;
        case AttributeRequirement::Operator::Equals:
            if (actual != expected)
                return false;
            break;
        case AttributeRequirement::Operator::Includes: {
            // `[class~=note]` - a whitespace-separated word matches. Scanned in place for the same
            // reason as the class test above (row 3j).
            bool found = false;
            size_t at = 0;
            while (at < actual.size()) {
                auto const start = actual.find_first_not_of(" \t\r\n\f", at);
                if (start == std::string::npos)
                    break;
                auto const end = actual.find_first_of(" \t\r\n\f", start);
                auto const stop = end == std::string::npos ? actual.size() : end;
                if (stop - start == expected.size() && actual.compare(start, expected.size(), expected) == 0) {
                    found = true;
                    break;
                }
                at = stop;
            }
            if (!found)
                return false;
            break;
        }
        case AttributeRequirement::Operator::DashMatch:
            // `[lang|=en]` - equals, or starts with the value followed by '-'.
            if (actual != expected
                && !(actual.size() > expected.size() && actual.compare(0, expected.size(), expected) == 0
                    && actual[expected.size()] == '-'))
                return false;
            break;
        case AttributeRequirement::Operator::Prefix:
            if (actual.size() < expected.size() || actual.compare(0, expected.size(), expected) != 0)
                return false;
            break;
        case AttributeRequirement::Operator::Suffix:
            if (actual.size() < expected.size()
                || actual.compare(actual.size() - expected.size(), expected.size(), expected) != 0)
                return false;
            break;
        case AttributeRequirement::Operator::Substring:
            if (actual.find(expected) == std::string::npos)
                return false;
            break;
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
        // Row 3j: this used to build the node's class list with an istringstream and a
        // std::vector<std::string> on every (rule x node) test - measured at ~8 us per test, which
        // was most of what was left. A class is a whole whitespace-separated word, so it is found
        // by scanning for word boundaries, with no allocation.
        for (auto const& required : classes) {
            bool found = false;
            size_t at = 0;
            while (at < node_classes->size()) {
                auto const start = node_classes->find_first_not_of(" \t\r\n\f", at);
                if (start == std::string::npos)
                    break;
                auto const end = node_classes->find_first_of(" \t\r\n\f", start);
                auto const stop = end == std::string::npos ? node_classes->size() : end;
                if (stop - start == required.size()
                    && node_classes->compare(start, required.size(), required) == 0) {
                    found = true;
                    break;
                }
                at = stop;
            }
            if (!found)
                return false;
        }
    }
    for (auto const& pseudo : pseudo_classes) {
        // Structural pseudo-classes need no interaction state.
        if (pseudo == "first-child" || pseudo == "last-child" || pseudo == "only-child"
            || pseudo == "empty" || pseudo == "root"
            || pseudo == "first-of-type" || pseudo == "last-of-type" || pseudo == "only-of-type"
            || pseudo.rfind("nth-child(", 0) == 0 || pseudo.rfind("nth-last-child(", 0) == 0
            || pseudo.rfind("nth-of-type(", 0) == 0 || pseudo.rfind("nth-last-of-type(", 0) == 0) {
            if (!matches_structural_pseudo(node, pseudo))
                return false;
            continue;
        }
        // `:has(<relative selector list>)`. Upstream (Rust/src/css/style/component_matching.rs):
        //   Pc::Has => self.matches_relative(arguments, node, node, context),
        // A relative selector is a combinator plus a selector, the combinator being a descendant
        // unless written. The anchor is the element itself and the search is over what it contains,
        // so `:has(> .x)` looks at children and `:has(.x)` at every descendant.
        //
        // The sibling combinators are not matched anywhere in this engine, so `:has(+ .x)` and
        // `:has(~ .x)` are answered false rather than guessed at.
        if (pseudo.rfind("has(", 0) == 0) {
            auto close = pseudo.rfind(')');
            if (close == std::string::npos || close <= 4)
                return false;
            auto inner = pseudo.substr(4, close - 4);
            bool any = false;
            for (auto const& part : split_selector_list(inner)) {
                if (part.empty())
                    continue;
                char const lead = part.front();
                bool const child_only = lead == '>';
                bool const next_only = lead == '+';
                bool const any_following = lead == '~';
                auto body = (child_only || next_only || any_following || lead == ' ') ? part.substr(1) : part;
                auto begin = body.find_first_not_of(" \t\r\n");
                if (begin == std::string::npos)
                    continue;
                body = body.substr(begin);

                std::function<bool(DomNode const&)> search = [&](DomNode const& parent) {
                    for (auto const& child : parent.children) {
                        if (child->type != DomNodeType::Element)
                            continue;
                        if (matches(*child, body, interaction_state))
                            return true;
                        if (!child_only && search(*child))
                            return true;
                    }
                    return false;
                };
                // The sibling forms look forward, not down: `:has(+ .x)` is "the element after me
                // matches". Upstream reaches them through the same relative-selector walk.
                auto next_element_sibling = [](DomNode const& from) -> DomNode const* {
                    auto const* parent = from.parent;
                    if (!parent)
                        return nullptr;
                    bool seen = false;
                    for (auto const& child : parent->children) {
                        if (child.get() == &from) {
                            seen = true;
                            continue;
                        }
                        if (seen && child->type == DomNodeType::Element)
                            return child.get();
                    }
                    return nullptr;
                };

                bool matched = false;
                if (next_only || any_following) {
                    for (auto const* sibling = next_element_sibling(node); sibling;
                         sibling = next_element_sibling(*sibling)) {
                        if (matches(*sibling, body, interaction_state)) {
                            matched = true;
                            break;
                        }
                        if (next_only)
                            break;
                    }
                } else {
                    matched = search(node);
                }
                if (matched) {
                    any = true;
                    break;
                }
            }
            if (!any)
                return false;
            continue;
        }

        // The list-taking pseudo-classes. Upstream (Rust/src/css/style/component_matching.rs):
        //   Pc::Is | Pc::Where => self.matches_any(arguments, ...)
        //   Pc::Not           => Ok(!self.matches_any(arguments, ...))
        // so all three take a selector list and the arguments go back through matches(), which
        // means a complex argument like `:is(.card .title)` works.
        {
            std::string_view const list_takers[] = { "is(", "where(", "not(" };
            std::string_view const* taker = nullptr;
            for (auto const& candidate : list_takers)
                if (pseudo.rfind(candidate, 0) == 0)
                    taker = &candidate;
            if (taker) {
                auto close = pseudo.rfind(')');
                if (close == std::string::npos || close <= taker->size())
                    return false;
                auto inner = pseudo.substr(taker->size(), close - taker->size());
                bool any = false;
                for (auto const& part : split_selector_list(inner)) {
                    if (matches(node, part, interaction_state)) {
                        any = true;
                        break;
                    }
                }
                bool const negated = *taker == "not(";
                if (any == negated)
                    return false;
                continue;
            }
        }

        // Form states, from the attributes this engine keeps: `el.checked = true` writes the
        // `checked` attribute, so these are attribute facts and need no interaction state.
        // Upstream publishes them as element facts (matches_checked_pseudo_class() and friends).
        if (pseudo == "checked" || pseudo == "unchecked") {
            bool const checked = node.has_attribute("checked") || node.has_attribute("selected");
            if (pseudo == "checked" && !checked)
                return false;
            if (pseudo == "unchecked" && checked)
                return false;
            continue;
        }
        if (pseudo == "disabled" || pseudo == "enabled") {
            bool const disabled = node.has_attribute("disabled");
            if (pseudo == "disabled" && !disabled)
                return false;
            if (pseudo == "enabled" && disabled)
                return false;
            continue;
        }
        if (pseudo == "placeholder-shown") {
            // A control with a placeholder and no value of its own. The value lives in the `value`
            // attribute here (`el.value = x` writes it), so an empty or absent attribute is "no
            // value".
            bool const has_placeholder = node.has_attribute("placeholder");
            auto const* value = node.attribute("value");
            bool const empty = !value || value->empty();
            if (!has_placeholder || !empty)
                return false;
            continue;
        }

        if (!interaction_state) return false;
        if (pseudo == "hover" && interaction_state->hovered != &node) return false;
        if (pseudo == "focus" && interaction_state->focused != &node) return false;
        if (pseudo == "focus-visible") {
            // Upstream's Element::should_indicate_focus(): a text-entry control
            // always indicates focus; anything else only when the focus arrived
            // from the keyboard rather than a pointer.
            if (interaction_state->focused != &node) return false;
            bool text_entry = node.name == "input" || node.name == "textarea" || node.name == "select";
            if (!text_entry && interaction_state->focus_trigger != FocusTrigger::Key) return false;
            continue;
        }
        if (pseudo == "focus-within") {
            // Upstream builds a FocusWithinChain from the focused element and asks
            // whether the element is on it (SelectorMatching.cpp). Same test here:
            // walk up from the focused node and look for this one.
            bool within = false;
            for (auto const* ancestor = interaction_state->focused; ancestor; ancestor = ancestor->parent) {
                if (ancestor == &node) {
                    within = true;
                    break;
                }
            }
            if (!within) return false;
        }
        if (pseudo == "active" && interaction_state->active != &node) return false;
        if (pseudo != "hover" && pseudo != "focus" && pseudo != "active" && pseudo != "focus-within") return false;
    }
    return true;
}
// The cascade weight. It is computed once per selector, at compile time; this stays as a member
// because the pseudo-element path, which matches a base selector by text, still asks for it.
int StyleResolver::specificity(std::string const& selector)
{
    return selector_specificity(selector);
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

    if (declaration.property == "flex") {
        // The `flex` shorthand, css-flexbox-1 §7.1: `flex: <grow> <shrink>? <basis>?`. It matters because it is
        // how real pages size a column - `flex: 1` means `1 1 0%`, not "grow with an auto basis" - and without
        // it every `flex: 1` in the wild was ignored (row 3c1).
        std::istringstream stream(declaration.value);
        std::vector<std::string> parts;
        std::string part;
        while (stream >> part)
            parts.push_back(part);
        auto is_number = [](std::string const& token) {
            char* end = nullptr;
            std::strtof(token.c_str(), &end);
            return end != token.c_str() && end && *end == '\0';
        };
        if (parts.size() == 1 && parts[0] == "none") {
            properties.set("flex-grow", "0");
            properties.set("flex-shrink", "0");
            properties.set("flex-basis", "auto");
            return;
        }
        if (parts.size() == 1 && parts[0] == "auto") {
            properties.set("flex-grow", "1");
            properties.set("flex-shrink", "1");
            properties.set("flex-basis", "auto");
            return;
        }
        if (parts.size() == 1 && parts[0] == "initial") {
            properties.set("flex-grow", "0");
            properties.set("flex-shrink", "1");
            properties.set("flex-basis", "auto");
            return;
        }
        float grow = 0;
        float shrink = 1;
        std::string basis = "0%";
        std::size_t index = 0;
        if (index < parts.size() && is_number(parts[index]))
            grow = std::strtof(parts[index++].c_str(), nullptr);
        if (index < parts.size() && is_number(parts[index]))
            shrink = std::strtof(parts[index++].c_str(), nullptr);
        if (index < parts.size())
            basis = parts[index];
        properties.set("flex-grow", std::to_string(grow));
        properties.set("flex-shrink", std::to_string(shrink));
        properties.set("flex-basis", basis);
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

    if (declaration.property == "border") {
        // <width> <style> <color>, in any order.
        std::istringstream stream(declaration.value);
        std::string token;
        while (stream >> token) {
            if (token == "solid" || token == "dashed" || token == "dotted" || token == "none") {
                properties.set("border-style", token);
            } else if (std::isdigit(static_cast<unsigned char>(token[0])) || token[0] == '.') {
                properties.set("border-top-width", token);
                properties.set("border-right-width", token);
                properties.set("border-bottom-width", token);
                properties.set("border-left-width", token);
            } else {
                properties.set("border-color", token);
            }
        }
        return;
    }

    if (declaration.property == "background") {
        auto const& value = declaration.value;
        auto url_pos = value.find("url(");
        if (url_pos != std::string::npos) {
            auto url_end = value.find(')', url_pos);
            properties.set("background-image", value.substr(url_pos, url_end == std::string::npos ? std::string::npos : url_end - url_pos + 1));
        }
        std::istringstream stream(value);
        std::string token;
        while (stream >> token) {
            if (token.find("url(") != std::string::npos)
                continue;
            if (token == "repeat" || token == "no-repeat" || token == "repeat-x" || token == "repeat-y"
                || token == "center" || token == "left" || token == "right" || token == "top" || token == "bottom"
                || token == "fixed" || token == "scroll" || token == "cover" || token == "contain")
                continue;
            properties.set("background-color", token);
            break;
        }
        return;
    }

    properties.set(declaration.property, declaration.value);
}

void StyleResolver::apply_initial_values(DomNode const& node, StyleProperties& properties)
{
    // Only fills properties that are still unset, so values coming from the
    // cascade or inheritance are never clobbered by an initial value.
    auto set_default = [&](std::string const& key, std::string value) {
        if (!properties.contains(key))
            properties.set(key, std::move(value));
    };

    bool is_form_control = node.type == DomNodeType::Element
        && (node.name == "input" || node.name == "textarea" || node.name == "select" || node.name == "button");

    std::string default_display = "block";
    if (node.type == DomNodeType::Text)
        default_display = "inline";
    else if (is_form_control)
        default_display = "inline-block";
    set_default("display", default_display);

    set_default("color", "black");
    set_default("font-size", "16px");
    set_default("line-height", "normal");
    set_default("white-space", "normal");
    set_default("opacity", "1");
    set_default("position", "static");
    set_default("border-style", "solid");
    set_default("border-radius", "0");
    set_default("overflow", "visible");
    set_default("z-index", "auto");
    set_default("background-repeat", "repeat");
    set_default("background-position", "0 0");
    set_default("background-size", "auto");
    set_default("object-fit", "fill");

    if (is_form_control) {
        set_default("border-width", "1px");
        set_default("padding", "4px");
        set_default("background-color", "white");
    }
}

} // namespace aetheris::rendering

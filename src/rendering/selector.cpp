#include "selector.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace aetheris::rendering {

// Splits a selector list on top-level commas, so `:is(.a, .b:not(.c, .d))` keeps its nesting.
std::vector<std::string> split_selector_list(std::string const& list)
{
    std::vector<std::string> parts;
    std::string current;
    int depth = 0;
    for (char c : list) {
        if (c == '(') {
            ++depth;
        } else if (c == ')') {
            --depth;
        } else if (c == ',' && depth == 0) {
            parts.push_back(current);
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.empty())
        parts.push_back(current);
    for (auto& part : parts) {
        auto begin = part.find_first_not_of(" \t\r\n");
        auto end = part.find_last_not_of(" \t\r\n");
        part = begin == std::string::npos ? std::string() : part.substr(begin, end - begin + 1);
    }
    parts.erase(std::remove(parts.begin(), parts.end(), std::string()), parts.end());
    return parts;
}

// Trims surrounding whitespace. The parser has its own; this one lives here so the
// matcher does not depend on the parser's internals.
static std::string trim_copy(std::string text)
{
    auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};
    auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

// Parses one compound selector (`p.card[data-x]:hover`) into its parts. This is the parse that
// used to run inside the matcher on every (node x rule) test; it runs once per selector now.
CompoundSelector parse_compound(std::string_view text)
{
    CompoundSelector result;
    if (text.empty())
        return result;

    std::string& tag = result.tag;
    std::string& id = result.id;
    std::vector<std::string>& classes = result.classes;
    std::vector<std::string>& pseudo_classes = result.pseudo_classes;
    std::vector<AttributeRequirement>& attributes = result.attributes;
    size_t i = 0;

    auto is_marker = [](char c) { return c == '#' || c == '.' || c == ':' || c == '['; };

    while (i < text.size() && !is_marker(text[i]))
        tag += text[i++];

    while (i < text.size()) {
        char marker = text[i++];
        if (marker == '[') {
            std::string inner;
            while (i < text.size() && text[i] != ']')
                inner += text[i++];
            if (i < text.size())
                ++i;
            auto equals = inner.find('=');
            if (equals == std::string::npos) {
                if (!inner.empty()) {
                    auto name = trim_copy(inner);
                    if (!name.empty())
                        attributes.push_back({ AttributeRequirement::Operator::Present, name, {}, false });
                }
            } else {
                // The operator is whatever sits before '=': nothing, '~', '|', '^', '$' or '*'.
                auto name = trim_copy(inner.substr(0, equals));
                char op_char = name.empty() ? 0 : name.back();
                auto op = AttributeRequirement::Operator::Equals;
                if (op_char == '~' || op_char == '|' || op_char == '^' || op_char == '$' || op_char == '*') {
                    name.pop_back();
                    name = trim_copy(name);
                    op = op_char == '~' ? AttributeRequirement::Operator::Includes
                        : op_char == '|' ? AttributeRequirement::Operator::DashMatch
                        : op_char == '^' ? AttributeRequirement::Operator::Prefix
                        : op_char == '$' ? AttributeRequirement::Operator::Suffix
                                         : AttributeRequirement::Operator::Substring;
                }
                std::string value = trim_copy(inner.substr(equals + 1));
                // A trailing flag, outside any quotes: ` i` case-insensitive, ` s` sensitive.
                bool insensitive = false;
                auto const flag_space = value.find_last_of(" \t");
                if (flag_space != std::string::npos) {
                    std::string flag = trim_copy(value.substr(flag_space + 1));
                    for (auto& c : flag)
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (flag == "i" || flag == "s") {
                        insensitive = flag == "i";
                        value = trim_copy(value.substr(0, flag_space));
                    }
                }
                if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'')
                    && value.back() == value.front())
                    value = value.substr(1, value.size() - 2);
                attributes.push_back({ op, name, value, insensitive });
            }
            continue;
        }
        std::string value;
        while (i < text.size() && !is_marker(text[i]))
            value += text[i++];
        // A functional pseudo-class: markers inside the parens must not end it, so if the capture
        // stopped with an unclosed paren, consume to its match.
        //
        // The first version of this only fired when the value *ended* with '(', which covered
        // `:not(.a)` and missed `:has(> .x)` - there the capture stopped at the '.' after a space,
        // so the pseudo text was "has(> " with no ')' and the whole rule matched nothing.
        if (marker == ':') {
            auto const opened = static_cast<int>(std::count(value.begin(), value.end(), '('));
            auto const closed = static_cast<int>(std::count(value.begin(), value.end(), ')'));
            int depth = opened - closed;
            while (i < text.size() && depth > 0) {
                if (text[i] == '(')
                    ++depth;
                else if (text[i] == ')')
                    --depth;
                value += text[i++];
            }
        }
        if (marker == '#')
            id = value;
        else if (marker == '.' && !value.empty())
            classes.push_back(value);
        else if (marker == ':' && !value.empty())
            pseudo_classes.push_back(value);
    }

    return result;
}

CompiledSelector compile_selector(std::string_view selector)
{
    CompiledSelector compiled;
    compiled.text = std::string(selector);
    compiled.specificity = selector_specificity(compiled.text);

    // Split into compound selectors separated by combinators: whitespace = descendant, '>' = child,
    // '+' = next sibling, '~' = subsequent sibling. Parentheses and brackets hold their own
    // selectors (`:is(.a, .b)`, `[a=v i]`), so depth is tracked and a space inside one is part of
    // the argument, not a combinator.
    //
    // This replaces two scans with one. The old code first asked whether the selector had any
    // combinator at all, using a lambda that re-walked the text from 0 for every character -
    // O(n^2) in selector length, on every (node x rule) test. Splitting unconditionally produces
    // the same parts for a selector with no combinator (one part, descendant by default) in one
    // linear pass.
    struct RawPart {
        char combinator { ' ' };
        std::string text;
    };
    std::vector<RawPart> raw;
    std::string current;
    char pending = ' ';
    bool have_part = false;

    auto flush = [&]() {
        if (have_part) {
            raw.push_back({ pending, current });
            current.clear();
            have_part = false;
        }
    };

    int depth = 0;
    for (char c : selector) {
        if (c == '(' || c == '[') {
            ++depth;
            current += c;
            have_part = true;
            continue;
        }
        if (c == ')' || c == ']') {
            if (depth > 0)
                --depth;
            current += c;
            have_part = true;
            continue;
        }
        if (depth > 0) {
            current += c;
            have_part = true;
            continue;
        }
        if (c == '>' || c == '+' || c == '~') {
            flush();
            pending = c;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            flush();
            // A space after a combinator does not replace it: `a > b`, `a + b`.
            if (pending != '>' && pending != '+' && pending != '~')
                pending = ' ';
        } else {
            current += c;
            have_part = true;
        }
    }
    flush();

    compiled.parts.reserve(raw.size());
    for (auto& part : raw)
        compiled.parts.push_back({ part.combinator, parse_compound(part.text) });
    return compiled;
}

int selector_specificity(std::string const& selector)
{
    auto is_ident_char = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
    };

    int ids = 0;
    int classes = 0;
    int elements = 0;

    size_t i = 0;
    while (i < selector.size()) {
        char c = selector[i];
        if (c == '#') {
            ++ids;
            ++i;
            while (i < selector.size() && is_ident_char(selector[i]))
                ++i;
        } else if (c == '.') {
            ++classes;
            ++i;
            while (i < selector.size() && is_ident_char(selector[i]))
                ++i;
        } else if (c == '[') {
            ++classes;
            while (i < selector.size() && selector[i] != ']')
                ++i;
            if (i < selector.size())
                ++i;
        } else if (c == ':') {
            ++i;
            if (i < selector.size() && selector[i] == ':')
                ++i; // pseudo-element marker; the name is counted once
            auto const name_begin = i;
            while (i < selector.size() && is_ident_char(selector[i]))
                ++i;
            std::string const name = selector.substr(name_begin, i - name_begin);
            // Upstream (Rust/src/css/selector_arena.rs, specificity()):
            //   Has | Is | Not => greatest(argument_selector_list)
            //   Where          => Specificity::default()
            //   everything else => one class
            if (i < selector.size() && selector[i] == '(' && (name == "is" || name == "where" || name == "not")) {
                auto close = selector.find(')', i);
                if (close == std::string::npos)
                    return ids * 10000 + classes * 100 + elements;
                std::string const inner = selector.substr(i + 1, close - i - 1);
                if (name != "where") {
                    // The greatest argument, compared component-wise: an int max would pick
                    // 100 classes over 1 id, which is the wrong way round.
                    int best_ids = 0;
                    int best_classes = 0;
                    int best_elements = 0;
                    bool have_best = false;
                    for (auto const& part : split_selector_list(inner)) {
                        int const value = selector_specificity(part);
                        int const part_ids = value / 10000;
                        int const part_classes = (value / 100) % 100;
                        int const part_elements = value % 100;
                        bool const better = !have_best || part_ids > best_ids
                            || (part_ids == best_ids && part_classes > best_classes)
                            || (part_ids == best_ids && part_classes == best_classes && part_elements > best_elements);
                        if (better) {
                            best_ids = part_ids;
                            best_classes = part_classes;
                            best_elements = part_elements;
                            have_best = true;
                        }
                    }
                    ids += best_ids;
                    classes += best_classes;
                    elements += best_elements;
                }
                i = close + 1;
            } else {
                ++classes;
            }
        } else if (std::isspace(static_cast<unsigned char>(c)) || c == '>' || c == '+' || c == '~' || c == '*') {
            ++i;
        } else {
            ++elements;
            ++i;
            while (i < selector.size() && is_ident_char(selector[i]))
                ++i;
        }
    }
    return ids * 10000 + classes * 100 + elements;
}

} // namespace aetheris::rendering

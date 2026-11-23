#include "content_value.hpp"

namespace aetheris::rendering {

namespace {

bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

// Splits a part of the declaration into string items. A quoted run is one item; anything else is a token
// this engine does not support (`attr(...)`, `counter(...)`, `open-quote`), counted rather than rendered, so
// `content: attr(title)` generates an empty box instead of the literal text "attr(title)".
void collect_items(std::string_view part, std::vector<std::string>& items, std::size_t& unsupported)
{
    std::size_t index = 0;
    while (index < part.size()) {
        if (is_space(part[index])) {
            ++index;
            continue;
        }
        if (part[index] != '\'') {
            // Skip the unsupported token: up to the next space, or to a closing paren for a function.
            std::size_t end = index;
            int depth = 0;
            while (end < part.size()) {
                if (part[end] == '(')
                    ++depth;
                else if (part[end] == ')')
                    --depth;
                else if (is_space(part[end]) && depth <= 0)
                    break;
                ++end;
            }
            ++unsupported;
            index = end;
            continue;
        }
        // A quoted run, with `\'` and `\\` as the only escapes the writer produces.
        std::string item;
        ++index;
        while (index < part.size() && part[index] != '\'') {
            if (part[index] == '\\' && index + 1 < part.size()) {
                ++index;
                item += part[index];
                ++index;
                continue;
            }
            item += part[index];
            ++index;
        }
        if (index < part.size())
            ++index; // the closing quote
        items.push_back(std::move(item));
    }
}

std::string join(std::vector<std::string> const& parts)
{
    std::string out;
    for (auto const& part : parts)
        out += part;
    return out;
}

} // namespace

std::string ContentValue::text() const { return join(items); }

std::string ContentValue::alt() const { return join(alt_text); }

ContentValue parse_content_value(std::string_view raw)
{
    ContentValue value;

    // The declaration as it arrives, trimmed. `none` and `normal` are keywords, not item lists.
    std::size_t begin = 0;
    std::size_t end = raw.size();
    while (begin < end && is_space(raw[begin]))
        ++begin;
    while (end > begin && is_space(raw[end - 1]))
        --end;
    auto const trimmed = raw.substr(begin, end - begin);
    if (trimmed == "none") {
        value.kind = ContentValue::Kind::None;
        return value;
    }
    if (trimmed == "normal") {
        value.kind = ContentValue::Kind::Normal;
        return value;
    }

    // A top-level `/` separates the content from its alternative text (css-content-3). Quoted runs cannot
    // contain an unescaped `/` that matters here, so the scan skips them.
    std::size_t slash = std::string_view::npos;
    bool in_quotes = false;
    for (std::size_t index = 0; index < trimmed.size(); ++index) {
        auto const c = trimmed[index];
        if (c == '\\' && in_quotes) {
            ++index;
            continue;
        }
        if (c == '\'') {
            in_quotes = !in_quotes;
            continue;
        }
        if (!in_quotes && c == '/') {
            slash = index;
            break;
        }
    }

    value.kind = ContentValue::Kind::List;
    if (slash == std::string_view::npos) {
        collect_items(trimmed, value.items, value.unsupported_items);
    } else {
        collect_items(trimmed.substr(0, slash), value.items, value.unsupported_items);
        collect_items(trimmed.substr(slash + 1), value.alt_text, value.unsupported_items);
    }
    // An empty declaration (`content: ''`) is a list holding one empty string, and it generates a box. A
    // declaration whose every item was unsupported also generates one, empty - which is what a browser does
    // for an `attr()` it cannot resolve.
    if (value.items.empty())
        value.items.emplace_back();
    return value;
}

} // namespace aetheris::rendering

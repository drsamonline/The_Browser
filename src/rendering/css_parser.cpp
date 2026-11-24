#include "css_parser.hpp"

#include <algorithm>
#include <cctype>

namespace aetheris::rendering {

CssStyleSheet CssParser::parse(std::string_view input)
{
    CssTokenizer tokenizer(input);
    return parse_tokens(tokenizer.tokenize());
}

CssStyleSheet CssParser::parse_tokens(std::vector<CssToken> const& tokens)
{
    CssStyleSheet sheet;
    size_t position = 0;
    size_t source_order = 0;
    parse_block(tokens, position, sheet, source_order, true);
    return sheet;
}

// Consumes tokens up to and including the matching close brace.
void CssParser::skip_block(std::vector<CssToken> const& tokens, size_t& position)
{
    int depth = 1;
    while (position < tokens.size() && tokens[position].type != CssTokenType::EndOfFile) {
        auto type = tokens[position++].type;
        if (type == CssTokenType::OpenBrace)
            ++depth;
        else if (type == CssTokenType::CloseBrace && --depth == 0)
            return;
    }
}

// Reads `@font-face { font-family: X; src: url(...) }` into the stylesheet's face table. The block is consumed
// whole, whether or not it named a family: a half-read block would leave the token position inside a rule.
void CssParser::parse_font_face(std::vector<CssToken> const& tokens, size_t& position, CssStyleSheet& sheet)
{
    CssFontFace face;
    int depth = 1;
    while (position < tokens.size() && tokens[position].type != CssTokenType::EndOfFile) {
        auto const type = tokens[position].type;
        if (type == CssTokenType::OpenBrace) {
            ++depth;
            ++position;
            continue;
        }
        if (type == CssTokenType::CloseBrace) {
            if (--depth == 0) {
                ++position;
                break;
            }
            ++position;
            continue;
        }
        // property ":" value ";"
        auto property_end = position;
        while (property_end < tokens.size() && tokens[property_end].type != CssTokenType::Colon
            && tokens[property_end].type != CssTokenType::Semicolon
            && tokens[property_end].type != CssTokenType::CloseBrace)
            ++property_end;
        std::string property;
        for (size_t index = position; index < property_end; ++index)
            if (is_significant(tokens[index]))
                property += tokens[index].value;
        position = property_end;
        if (position < tokens.size() && tokens[position].type == CssTokenType::Colon)
            ++position;
        // A value runs to the `;` or `}` that ends it, but not one inside parentheses: a `data:` URI
        // carries `data:font/ttf;base64,AAAA`, and reading that `;` as the end of the declaration cut the
        // source at `url(data:font/ttf` so the face had no source and was dropped (row 3d1's tail). This is
        // the same nested-syntax rule the selector scanners needed, applied to declaration values.
        auto value_begin = position;
        int parens = 0;
        while (position < tokens.size() && tokens[position].type != CssTokenType::EndOfFile) {
            if (parens == 0 && (tokens[position].type == CssTokenType::Semicolon
                    || tokens[position].type == CssTokenType::CloseBrace))
                break;
            for (char const c : tokens[position].value) {
                if (c == '(')
                    ++parens;
                else if (c == ')' && parens > 0)
                    --parens;
            }
            ++position;
        }
        std::string value;
        for (size_t index = value_begin; index < position; ++index)
            value += tokens[index].value;
        value = trim(value);
        if (property == "font-family") {
            // The family may be quoted; the tokenizer already removed the quotes.
            face.family = value;
        } else if (property == "src") {
            // `url(...)` with the file inside, possibly followed by a format hint.
            auto const open = value.find("url(");
            if (open != std::string::npos) {
                auto const close = value.find(')', open);
                if (close != std::string::npos)
                    face.source = trim(value.substr(open + 4, close - open - 4));
            }
        }
        if (position < tokens.size() && tokens[position].type == CssTokenType::Semicolon)
            ++position;
    }
    if (!face.family.empty() && !face.source.empty())
        sheet.font_faces.push_back(std::move(face));
}

// Reads `@keyframes name { from { … } 50% { … } to { … } }` into the stylesheet. The outer block is already open
// when this is called; each inner block is one keyframe, and its offset comes from the tokens before its brace.
// `from`, `to`, `50%` or `0.5` - the offset a keyframe applies at.
static std::string trimmed(std::string text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.erase(text.begin());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.pop_back();
    return text;
}

static double offset_from_text(std::string text)
{
    text = trimmed(std::move(text));
    if (text == "from")
        return 0.0;
    if (text == "to")
        return 1.0;
    bool const percent = !text.empty() && text.back() == '%';
    if (percent)
        text.pop_back();
    char* end = nullptr;
    double const value = std::strtod(text.c_str(), &end);
    if (end == text.c_str())
        return 0.0;
    return percent ? value / 100.0 : value;
}

void CssParser::parse_keyframes(std::string const& name, std::vector<CssToken> const& tokens, size_t& position, CssStyleSheet& sheet)
{
    KeyframesRule rule;
    rule.name = name;

    // The caller consumed the rule's own opening brace before dispatching here, as it does for every block: the
    // first token is already the first keyframe's offset.
    int depth = 1;
    while (position < tokens.size() && tokens[position].type != CssTokenType::EndOfFile) {
        // Between keyframes: read the offset text up to the next brace.
        std::string offset_text;
        while (position < tokens.size() && tokens[position].type != CssTokenType::OpenBrace
            && tokens[position].type != CssTokenType::CloseBrace && tokens[position].type != CssTokenType::EndOfFile) {
            if (is_significant(tokens[position]))
                offset_text += tokens[position].value;
            ++position;
        }
        if (position >= tokens.size() || tokens[position].type == CssTokenType::EndOfFile)
            break;
        if (tokens[position].type == CssTokenType::CloseBrace) {
            ++position;
            if (--depth == 0)
                break;
            continue;
        }
        ++position; // the keyframe's opening brace
        ++depth;

        Keyframe frame;
        frame.offset = offset_from_text(offset_text);
        // Declarations, the same property/colon/value shape the ordinary rule path reads.
        while (position < tokens.size() && tokens[position].type != CssTokenType::CloseBrace
            && tokens[position].type != CssTokenType::EndOfFile) {
            std::string property;
            while (position < tokens.size() && tokens[position].type != CssTokenType::Colon
                && tokens[position].type != CssTokenType::Semicolon
                && tokens[position].type != CssTokenType::CloseBrace) {
                if (is_significant(tokens[position]))
                    property += tokens[position].value;
                ++position;
            }
            if (position < tokens.size() && tokens[position].type == CssTokenType::Colon)
                ++position;
            std::string value;
            while (position < tokens.size() && tokens[position].type != CssTokenType::Semicolon
                && tokens[position].type != CssTokenType::CloseBrace) {
                value += tokens[position].value;
                ++position;
            }
            value = trim(value);
            if (!property.empty() && !value.empty())
                frame.declarations.emplace_back(property, value);
            if (position < tokens.size() && tokens[position].type == CssTokenType::Semicolon)
                ++position;
        }
        if (position < tokens.size() && tokens[position].type == CssTokenType::CloseBrace) {
            ++position;
            --depth;
        }
        rule.frames.push_back(std::move(frame));
    }

    std::sort(rule.frames.begin(), rule.frames.end(),
        [](Keyframe const& a, Keyframe const& b) { return a.offset < b.offset; });
    if (!rule.name.empty())
        sheet.keyframes.push_back(std::move(rule));
}

// The renderer always presents a screen viewport of 1024x768, so a media query
// is resolved against that: unknown features do not match.
bool CssParser::media_matches(std::string const& query)
{
    constexpr float kViewportWidth = 1024.0f;
    constexpr float kViewportHeight = 768.0f;

    auto evaluate = [&](std::string const& part) -> bool {
        std::string text;
        for (char c : part)
            if (!std::isspace(static_cast<unsigned char>(c)))
                text += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (text.empty())
            return false;
        if (text == "all" || text == "screen")
            return true;
        if (text == "print" || text == "speech")
            return false;

        // (min-width: 600px) / (max-width: 500px) / (min-height: ...)
        auto open = text.find('(');
        auto close = text.rfind(')');
        if (open == std::string::npos || close == std::string::npos || close < open)
            return false;
        std::string feature = text.substr(open + 1, close - open - 1);
        auto colon = feature.find(':');
        if (colon == std::string::npos)
            return false;
        std::string name = feature.substr(0, colon);
        std::string value = feature.substr(colon + 1);
        auto px = value.find("px");
        if (px != std::string::npos)
            value = value.substr(0, px);
        char* end = nullptr;
        float number = std::strtof(value.c_str(), &end);
        if (end == value.c_str())
            return false;
        if (name == "min-width")
            return kViewportWidth >= number;
        if (name == "max-width")
            return kViewportWidth <= number;
        if (name == "min-height")
            return kViewportHeight >= number;
        if (name == "max-height")
            return kViewportHeight <= number;
        return false;
    };

    // Comma-separated queries are alternatives; "and"-joined parts must all hold.
    std::size_t start = 0;
    while (start <= query.size()) {
        auto comma = query.find(',', start);
        std::string alternative = query.substr(start, comma == std::string::npos ? std::string::npos : comma - start);

        bool matched = true;
        std::size_t part_start = 0;
        while (part_start <= alternative.size()) {
            auto and_at = alternative.find("and", part_start);
            std::string part = alternative.substr(part_start,
                and_at == std::string::npos ? std::string::npos : and_at - part_start);
            if (!part.empty() && !evaluate(part)) {
                matched = false;
                break;
            }
            if (and_at == std::string::npos)
                break;
            part_start = and_at + 3;
        }
        if (matched)
            return true;
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return false;
}

void CssParser::parse_block(std::vector<CssToken> const& tokens, size_t& position,
    CssStyleSheet& sheet, size_t& source_order, bool include)
{
    while (position < tokens.size() && tokens[position].type != CssTokenType::EndOfFile) {
        while (position < tokens.size() && !is_significant(tokens[position]))
            ++position;
        if (position >= tokens.size() || tokens[position].type == CssTokenType::EndOfFile)
            break;
        // A close brace at block level ends this block (the @media case).
        if (tokens[position].type == CssTokenType::CloseBrace) {
            ++position;
            return;
        }

        std::vector<std::string> selectors;
        std::string selector;
        bool found_open_brace = false;
        // A comma inside parentheses is part of one selector, not a list separator:
        // `:is(#a, #b)` was split into `:is(#a` and `#b)` and `#b)`, which then matched as a bare
        // id selector. Depth is tracked rather than assumed, because `:not(:is(.a, .b))` nests.
        int depth = 0;

        while (position < tokens.size() && tokens[position].type != CssTokenType::EndOfFile) {
            auto const& token = tokens[position++];
            if (token.type == CssTokenType::OpenBrace && depth == 0) {
                auto trimmed = trim(selector);
                if (!trimmed.empty())
                    selectors.push_back(std::move(trimmed));
                found_open_brace = true;
                break;
            }
            // A comma inside parentheses or brackets is part of one selector:
            // `:is(.a, .b)` and `[data-x="a,b"]` both used to be split.
            if (token.type == CssTokenType::OpenParen || token.value == "[")
                ++depth;
            else if ((token.type == CssTokenType::CloseParen || token.value == "]") && depth > 0)
                --depth;
            if (token.type == CssTokenType::Comma && depth == 0) {
                auto trimmed = trim(selector);
                if (!trimmed.empty())
                    selectors.push_back(std::move(trimmed));
                selector.clear();
                continue;
            }
            selector += token.value;
        }

        if (!found_open_brace)
            break;

        // @keyframes name { <offset> { declarations } ... } — a block of blocks, so it is read whole rather than
        // left to the selector path (row 3d3).
        if (!selectors.empty() && selectors.front().rfind("@keyframes", 0) == 0) {
            if (include) {
                auto name = selectors.front().substr(10);
                name = trimmed(name);
                parse_keyframes(name, tokens, position, sheet);
            } else {
                skip_block(tokens, position);
            }
            continue;
        }

        // @font-face { ... } — a font descriptor block, not a selector rule. Its declarations are read into the
        // stylesheet's face table so `font-family` can find them later (row 3d1).
        if (!selectors.empty() && selectors.front().rfind("@font-face", 0) == 0) {
            if (include)
                parse_font_face(tokens, position, sheet);
            else
                skip_block(tokens, position);
            continue;
        }

        // @media <query> { rules } — nested rules, filtered by the viewport.
        if (!selectors.empty() && selectors.front().rfind("@media", 0) == 0) {
            std::string query = selectors.front().substr(6);
            bool matches = include && media_matches(query);
            parse_block(tokens, position, sheet, source_order, matches);
            continue;
        }

        if (!include) {
            skip_block(tokens, position);
            continue;
        }

        CssRule rule;
        rule.selectors = std::move(selectors);
        // Parse each selector once, here, rather than once per node at match time (row 3j).
        rule.compiled.reserve(rule.selectors.size());
        for (auto const& selector : rule.selectors)
            rule.compiled.push_back(compile_selector(selector));
        rule.source_order = source_order++;

        while (position < tokens.size() && tokens[position].type != CssTokenType::EndOfFile) {
            while (position < tokens.size() && tokens[position].type == CssTokenType::Whitespace)
                ++position;

            if (position >= tokens.size() || tokens[position].type == CssTokenType::CloseBrace) {
                if (position < tokens.size())
                    ++position;
                break;
            }

            std::string property;
            while (position < tokens.size() && tokens[position].type != CssTokenType::Colon
                && tokens[position].type != CssTokenType::CloseBrace) {
                property += tokens[position].value;
                ++position;
            }

            property = trim(std::move(property));
            if (position >= tokens.size() || tokens[position].type == CssTokenType::CloseBrace) {
                if (position < tokens.size())
                    ++position;
                break;
            }

            ++position;
            size_t value_begin = position;
            while (position < tokens.size() && tokens[position].type != CssTokenType::Semicolon
                && tokens[position].type != CssTokenType::CloseBrace)
                ++position;

            auto value = trim(join_value(tokens, value_begin, position));
            // `content` keeps the quotes of *each* string item, because the item list is what the property
            // means: `content: "a" "b"` is two items and renders "ab", not "a b". Upstream models it as a
            // StyleValueList plus an optional alt-text list (CSS/StyleValues/ContentStyleValue.h) and asks
            // the tree builder for Normal, None or List (Rust/src/layout/tree_builder.rs:1714);
            // ContentValue in content_value.hpp is that shape, and this is the one place the quotes are
            // written down. `content: ''` therefore arrives as `''` - a list holding one empty string -
            // which is how it stays distinguishable from no declaration at all.
            // `grid-template-areas` is a list of quoted rows for the same reason `content` is a list of quoted
            // items: the quotes are the structure, and joining the tokens throws it away (row 3c2).
            if (property == "content" || property == "grid-template-areas")
                value = trim(quote_string_items(tokens, value_begin, position));
            if (!property.empty())
                rule.declarations.push_back({ std::move(property), std::move(value) });

            if (position < tokens.size() && tokens[position].type == CssTokenType::Semicolon)
                ++position;
            else if (position < tokens.size() && tokens[position].type == CssTokenType::CloseBrace) {
                ++position;
                break;
            }
        }

        if (!rule.selectors.empty()) {
            for (auto const& selector : rule.selectors)
                sheet.note_selector(selector);
            sheet.rules.push_back(std::move(rule));
        }
    }
}

std::string CssParser::trim(std::string value)
{
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])))
        ++begin;
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;
    return value.substr(begin, end - begin);
}

std::string CssParser::join_value(std::vector<CssToken> const& tokens, size_t begin, size_t end)
{
    std::string value;
    for (size_t i = begin; i < end; ++i)
        value += tokens[i].value;
    return value;
}

// `content` values, with each string token written back as a quoted run so the item boundaries survive into
// the declaration string. Quotes and backslashes inside an item are escaped, so `content: "it's"` comes back
// out of ContentValue intact.
std::string CssParser::quote_string_items(std::vector<CssToken> const& tokens, size_t begin, size_t end)
{
    std::string value;
    for (size_t i = begin; i < end; ++i) {
        if (tokens[i].type != CssTokenType::String) {
            value += tokens[i].value;
            continue;
        }
        value += '\'';
        for (char c : tokens[i].value) {
            if (c == '\'' || c == '\\')
                value += '\\';
            value += c;
        }
        value += '\'';
    }
    return value;
}

bool CssParser::is_significant(CssToken const& token)
{
    return token.type != CssTokenType::Whitespace;
}

} // namespace aetheris::rendering

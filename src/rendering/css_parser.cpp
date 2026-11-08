#include "css_parser.hpp"

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

        while (position < tokens.size() && tokens[position].type != CssTokenType::EndOfFile) {
            auto const& token = tokens[position++];
            if (token.type == CssTokenType::OpenBrace) {
                auto trimmed = trim(selector);
                if (!trimmed.empty())
                    selectors.push_back(std::move(trimmed));
                found_open_brace = true;
                break;
            }
            if (token.type == CssTokenType::Comma) {
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
            if (!property.empty())
                rule.declarations.push_back({ std::move(property), std::move(value) });

            if (position < tokens.size() && tokens[position].type == CssTokenType::Semicolon)
                ++position;
            else if (position < tokens.size() && tokens[position].type == CssTokenType::CloseBrace) {
                ++position;
                break;
            }
        }

        if (!rule.selectors.empty())
            sheet.rules.push_back(std::move(rule));
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

bool CssParser::is_significant(CssToken const& token)
{
    return token.type != CssTokenType::Whitespace;
}

} // namespace aetheris::rendering

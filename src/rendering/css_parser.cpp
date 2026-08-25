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

    while (position < tokens.size() && tokens[position].type != CssTokenType::EndOfFile) {
        while (position < tokens.size() && !is_significant(tokens[position]))
            ++position;

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

        CssRule rule;
        rule.selectors = std::move(selectors);

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

            ++position; // colon
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

    return sheet;
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

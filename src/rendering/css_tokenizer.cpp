#include "css_tokenizer.hpp"

namespace aetheris::rendering {

CssTokenizer::CssTokenizer(std::string_view input) : m_input(input) {}

bool CssTokenizer::eof() const { return m_position >= m_input.size(); }

char CssTokenizer::peek(size_t offset) const
{
    auto position = m_position + offset;
    return position < m_input.size() ? m_input[position] : '\0';
}

void CssTokenizer::consume()
{
    if (eof())
        return;
    if (m_input[m_position] == '\n') {
        ++m_line;
        m_column = 1;
    } else {
        ++m_column;
    }
    ++m_position;
}

void CssTokenizer::skip_comment()
{
    consume();
    consume();
    while (!eof() && !(peek() == '*' && peek(1) == '/'))
        consume();
    if (!eof()) {
        consume();
        consume();
    }
}

CssToken CssTokenizer::make_token(CssTokenType type, std::string value, size_t line, size_t column) const
{
    return { type, std::move(value), line, column };
}

CssToken CssTokenizer::consume_identifier()
{
    auto line = m_line;
    auto column = m_column;
    std::string value;
    while (!eof() && is_identifier_char(peek())) {
        value += peek();
        consume();
    }
    return make_token(CssTokenType::Identifier, std::move(value), line, column);
}

CssToken CssTokenizer::consume_number()
{
    auto line = m_line;
    auto column = m_column;
    std::string value;
    if (peek() == '+' || peek() == '-') {
        value += peek();
        consume();
    }
    while (!eof() && ((peek() >= '0' && peek() <= '9') || peek() == '.')) {
        value += peek();
        consume();
    }
    return make_token(CssTokenType::Number, std::move(value), line, column);
}

CssToken CssTokenizer::consume_string(char quote)
{
    auto line = m_line;
    auto column = m_column;
    consume();
    std::string value;
    while (!eof() && peek() != quote) {
        if (peek() == '\\' && peek(1) != '\0')
            consume();
        value += peek();
        consume();
    }
    if (!eof())
        consume();
    return make_token(CssTokenType::String, std::move(value), line, column);
}

std::vector<CssToken> CssTokenizer::tokenize()
{
    std::vector<CssToken> tokens;
    while (!eof()) {
        auto line = m_line;
        auto column = m_column;
        auto c = peek();

        if (c == '/' && peek(1) == '*') {
            skip_comment();
            continue;
        }
        if (is_whitespace(c)) {
            std::string value;
            while (!eof() && is_whitespace(peek())) {
                value += peek();
                consume();
            }
            tokens.push_back(make_token(CssTokenType::Whitespace, std::move(value), line, column));
            continue;
        }
        if (is_identifier_start(c)) {
            tokens.push_back(consume_identifier());
            continue;
        }
        if ((c >= '0' && c <= '9') || ((c == '+' || c == '-') && ((peek(1) >= '0' && peek(1) <= '9') || peek(1) == '.'))) {
            tokens.push_back(consume_number());
            continue;
        }
        if (c == '"' || c == '\'') {
            tokens.push_back(consume_string(c));
            continue;
        }

        CssTokenType type = CssTokenType::Delimiter;
        switch (c) {
        case ':': type = CssTokenType::Colon; break;
        case ';': type = CssTokenType::Semicolon; break;
        case ',': type = CssTokenType::Comma; break;
        case '{': type = CssTokenType::OpenBrace; break;
        case '}': type = CssTokenType::CloseBrace; break;
        case '(': type = CssTokenType::OpenParen; break;
        case ')': type = CssTokenType::CloseParen; break;
        case '>': type = CssTokenType::ChildCombinator; break;
        case '*': type = CssTokenType::UniversalSelector; break;
        default: break;
        }
        std::string value(1, c);
        consume();
        tokens.push_back(make_token(type, std::move(value), line, column));
    }
    tokens.push_back(make_token(CssTokenType::EndOfFile, {}, m_line, m_column));
    return tokens;
}

bool CssTokenizer::is_whitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

bool CssTokenizer::is_identifier_start(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-'
        || c == '#' || c == '.';
}

bool CssTokenizer::is_identifier_char(char c)
{
    return is_identifier_start(c) || (c >= '0' && c <= '9');
}

} // namespace aetheris::rendering

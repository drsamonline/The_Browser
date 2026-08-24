#include "html_tokenizer.hpp"

namespace aetheris::rendering {

HtmlTokenizer::HtmlTokenizer(std::string_view input)
    : m_input(input)
{
}

char HtmlTokenizer::peek(size_t offset) const
{
    auto position = m_position + offset;
    return position < m_input.size() ? m_input[position] : '\0';
}

bool HtmlTokenizer::starts_with(std::string_view text) const
{
    return m_input.substr(m_position, text.size()) == text;
}

void HtmlTokenizer::consume()
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

void HtmlTokenizer::advance_text(std::string_view text)
{
    for (size_t i = 0; i < text.size() && !eof(); ++i)
        consume();
}

void HtmlTokenizer::skip_whitespace()
{
    while (!eof() && is_whitespace(peek()))
        consume();
}

HtmlToken HtmlTokenizer::make_token(HtmlTokenType type, std::string data, size_t line, size_t column) const
{
    HtmlToken token;
    token.type = type;
    token.data = std::move(data);
    token.line = line;
    token.column = column;
    return token;
}

HtmlToken HtmlTokenizer::parse_text()
{
    auto token_line = m_line;
    auto token_column = m_column;
    std::string text;

    while (!eof() && peek() != '<') {
        text += peek();
        consume();
    }

    return make_token(HtmlTokenType::Text, std::move(text), token_line, token_column);
}

HtmlToken HtmlTokenizer::parse_comment(size_t token_line, size_t token_column)
{
    advance_text("<!--");
    std::string data;

    while (!eof() && !starts_with("-->")) {
        data += peek();
        consume();
    }

    if (starts_with("-->"))
        advance_text("-->");

    return make_token(HtmlTokenType::Comment, std::move(data), token_line, token_column);
}

HtmlToken HtmlTokenizer::parse_doctype(size_t token_line, size_t token_column)
{
    advance_text("<!");
    while (!eof() && is_alpha(peek()))
        consume();

    skip_whitespace();
    std::string name;
    while (!eof() && peek() != '>')
    {
        name += peek();
        consume();
    }

    while (!name.empty() && is_whitespace(name.back()))
        name.pop_back();

    if (!eof() && peek() == '>')
        consume();

    return make_token(HtmlTokenType::Doctype, to_lower_copy(name), token_line, token_column);
}

HtmlToken HtmlTokenizer::parse_tag(bool is_end_tag, size_t token_line, size_t token_column)
{
    consume(); // '<'
    if (is_end_tag && peek() == '/')
        consume();

    skip_whitespace();

    std::string name;
    while (!eof() && is_alpha(peek())) {
        name += to_lower(peek());
        consume();
    }

    HtmlToken token = make_token(is_end_tag ? HtmlTokenType::EndTag : HtmlTokenType::StartTag,
        std::move(name), token_line, token_column);

    if (is_end_tag) {
        while (!eof() && peek() != '>')
            consume();
        if (!eof())
            consume();
        return token;
    }

    while (!eof()) {
        skip_whitespace();

        if (peek() == '>') {
            consume();
            return token;
        }

        if (peek() == '/' && peek(1) == '>') {
            token.type = HtmlTokenType::SelfClosingTag;
            token.is_self_closing = true;
            consume();
            consume();
            return token;
        }

        std::string attribute_name;
        while (!eof() && is_attribute_name_char(peek())) {
            attribute_name += to_lower(peek());
            consume();
        }

        if (attribute_name.empty()) {
            // Malformed markup: consume one character to guarantee progress.
            consume();
            continue;
        }

        skip_whitespace();
        std::string attribute_value;

        if (peek() == '=') {
            consume();
            skip_whitespace();

            if (peek() == '"' || peek() == '\'') {
                char quote = peek();
                consume();
                while (!eof() && peek() != quote) {
                    attribute_value += peek();
                    consume();
                }
                if (!eof())
                    consume();
            } else {
                while (!eof() && !is_whitespace(peek()) && peek() != '>' && !(peek() == '/' && peek(1) == '>')) {
                    attribute_value += peek();
                    consume();
                }
            }
        }

        token.attributes.insert_or_assign(std::move(attribute_name), std::move(attribute_value));
    }

    return token;
}

HtmlToken HtmlTokenizer::next_token()
{
    if (m_emitted_eof)
        return make_token(HtmlTokenType::EOFToken, {}, m_line, m_column);

    if (eof()) {
        m_emitted_eof = true;
        return make_token(HtmlTokenType::EOFToken, {}, m_line, m_column);
    }

    if (peek() != '<')
        return parse_text();

    auto token_line = m_line;
    auto token_column = m_column;

    if (starts_with("<!--"))
        return parse_comment(token_line, token_column);

    if (starts_with("<!DOCTYPE") || starts_with("<!doctype"))
        return parse_doctype(token_line, token_column);

    if (peek(1) == '/')
        return parse_tag(true, token_line, token_column);

    if (is_alpha(peek(1)))
        return parse_tag(false, token_line, token_column);

    // Not a recognized markup construct: '<' is ordinary text.
    consume();
    return make_token(HtmlTokenType::Text, "<", token_line, token_column);
}

bool HtmlTokenizer::is_whitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

bool HtmlTokenizer::is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool HtmlTokenizer::is_attribute_name_char(char c)
{
    return c != '\0' && !is_whitespace(c) && c != '=' && c != '/' && c != '>';
}

char HtmlTokenizer::to_lower(char c)
{
    if (c >= 'A' && c <= 'Z')
        return static_cast<char>(c - 'A' + 'a');
    return c;
}

std::string HtmlTokenizer::to_lower_copy(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (char c : text)
        result += to_lower(c);
    return result;
}

} // namespace aetheris::rendering

#include "html_tokenizer.hpp"
#include <iostream>

namespace aetheris::rendering {

HtmlTokenizer::HtmlTokenizer(std::string_view input)
    : m_input(input) {}

HtmlToken HtmlTokenizer::next_token() {
    while (!eof()) {
        switch (m_state) {
            case TokenizerState::Data: handle_data_state(); break;
            case TokenizerState::TagOpen: handle_tag_open_state(); break;
            case TokenizerState::EndTagOpen: handle_end_tag_open_state(); break;
            case TokenizerState::TagName: handle_tag_name_state(); break;
            case TokenizerState::BeforeAttributeName: handle_before_attribute_name_state(); break;
            case TokenizerState::AttributeName: handle_attribute_name_state(); break;
            // ... add more states as needed
            default: consume(); break;
        }
        if (!m_emitted_tokens.empty() && m_emitted_index < m_emitted_tokens.size()) {
            return m_emitted_tokens[m_emitted_index++];
        }
    }
    return {HtmlTokenType::EOFToken};
}

void HtmlTokenizer::consume() {
    if (m_position < m_input.size()) {
        char c = m_input[m_position];
        if (c == '\n') {
            m_line++;
            m_column = 1;
        } else {
            m_column++;
        }
        m_position++;
    }
}

void HtmlTokenizer::handle_data_state() {
    char c = m_input[m_position];
    if (c == '<') {
        m_state = TokenizerState::TagOpen;
        consume();
    } else {
        emit_character_token(c);
        consume();
    }
}

void HtmlTokenizer::handle_tag_open_state() {
    char c = m_input[m_position];
    if (c == '/') {
        m_state = TokenizerState::EndTagOpen;
        consume();
    } else if (is_alpha(c)) {
        m_state = TokenizerState::TagName;
        m_temp_buffer.clear();
        m_temp_buffer += to_lower(c);
        consume();
    } else {
        m_state = TokenizerState::Data;
        emit_character_token('<');
    }
}

void HtmlTokenizer::handle_end_tag_open_state() {
    char c = m_input[m_position];
    if (is_alpha(c)) {
        m_state = TokenizerState::TagName;
        m_temp_buffer.clear();
        m_temp_buffer += to_lower(c);
        consume();
    } else {
        m_state = TokenizerState::Data;
        emit_character_token('<');
        emit_character_token('/');
    }
}

void HtmlTokenizer::handle_tag_name_state() {
    char c = m_input[m_position];
    if (is_whitespace(c)) {
        m_state = TokenizerState::BeforeAttributeName;
        consume();
    } else if (c == '/') {
        m_state = TokenizerState::SelfClosingStartTag;
        consume();
    } else if (c == '>') {
        m_state = TokenizerState::Data;
    HtmlToken token;
        token.type = HtmlTokenType::StartTag;
    token.data = m_temp_buffer;
    m_emitted_tokens.push_back(token);
        consume();
    } else {
        m_temp_buffer += to_lower(c);
        consume();
}
}

void HtmlTokenizer::emit_character_token(char c) {
    HtmlToken token;
    token.type = HtmlTokenType::Text;
    m_temp_buffer = c;
    token.data = m_temp_buffer;
    m_emitted_tokens.push_back(token);
}

bool HtmlTokenizer::is_alpha(char c) const {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool HtmlTokenizer::is_whitespace(char c) const {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

char HtmlTokenizer::to_lower(char c) const {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

} // namespace aetheris::rendering

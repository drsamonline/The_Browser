#pragma once

/**
 * AETHERIS HTML TOKENIZER
 * Minimal HTML5 tokenizer for the Aetheris rendering engine
 *
 * Features:
 * - State machine-based tokenization
 * - Minimal memory allocations
 * - Error recovery for malformed HTML
 * - Position tracking for error reporting
 */

#ifndef AETHERIS_HTML_TOKENIZER_HPP
#define AETHERIS_HTML_TOKENIZER_HPP

#include <string_view>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <optional>

namespace aetheris::rendering {

// HTML token types
enum class HtmlTokenType : uint8_t {
    Doctype,
    StartTag,
    EndTag,
    SelfClosingTag,
    Text,
    Comment,
    EOFToken
};

// HTML token structure
struct HtmlToken {
    HtmlTokenType type;
    std::string_view data;
    std::unordered_map<std::string_view, std::string_view> attributes;
    size_t line = 1;
    size_t column = 1;
    bool is_self_closing = false;
};

// Tokenizer state
enum class TokenizerState : uint8_t {
    Data,
    TagOpen,
    EndTagOpen,
    TagName,
    BeforeAttributeName,
    AttributeName,
    AfterAttributeName,
    BeforeAttributeValue,
    AttributeValueDoubleQuoted,
    AttributeValueSingleQuoted,
    AttributeValueUnquoted,
    SelfClosingStartTag,
    CommentStart,
    CommentStartDash,
    Comment,
    CommentEndDash,
    CommentEnd,
    Doctype,
    DoctypeName,
    AfterDoctypeName,
    BogusComment
};

/**
 * HTML Tokenizer
 * Converts HTML input into a stream of tokens
 */
class HtmlTokenizer {
public:
    explicit HtmlTokenizer(std::string_view input);
    
    // Get the next token
    HtmlToken next_token();
    
    // Check if we've reached the end of input
    bool eof() const { return m_position >= m_input.size(); }
    
    // Get current line and column for error reporting
    size_t line() const { return m_line; }
    size_t column() const { return m_column; }
    
private:
    // State machine methods
    void consume();
    void reconsume();
    void emit_token(HtmlToken token);
    void emit_character_token(char c);
    void emit_eof_token();
    
    // State handlers
    void handle_data_state();
    void handle_tag_open_state();
    void handle_end_tag_open_state();
    void handle_tag_name_state();
    void handle_before_attribute_name_state();
    void handle_attribute_name_state();
    void handle_after_attribute_name_state();
    void handle_before_attribute_value_state();
    void handle_attribute_value_double_quoted_state();
    void handle_attribute_value_single_quoted_state();
    void handle_attribute_value_unquoted_state();
    void handle_self_closing_start_tag_state();
    void handle_comment_start_state();
    void handle_comment_start_dash_state();
    void handle_comment_state();
    void handle_comment_end_dash_state();
    void handle_comment_end_state();
    void handle_doctype_state();
    void handle_doctype_name_state();
    void handle_after_doctype_name_state();
    void handle_bogus_comment_state();
    
    // Helper methods
    void skip_whitespace();
    bool is_whitespace(char c) const;
    bool is_upper_alpha(char c) const;
    bool is_lower_alpha(char c) const;
    bool is_alpha(char c) const;
    bool is_digit(char c) const;
    bool is_attribute_name_char(char c) const;
    char to_lower(char c) const;
    
    // Input data
    std::string_view m_input;
    size_t m_position = 0;
    
    // Current state
    TokenizerState m_state = TokenizerState::Data;
    
    // Current token being built
    HtmlToken m_current_token;
    std::string m_temp_buffer;
    
    // Position tracking
    size_t m_line = 1;
    size_t m_column = 1;
    size_t m_token_start_line = 1;
    size_t m_token_start_column = 1;
    
    // Current attribute being processed
    std::string_view m_current_attribute_name;
    
    // Output tokens
    std::vector<HtmlToken> m_emitted_tokens;
    size_t m_emitted_index = 0;
};

} // namespace aetheris::rendering

#endif // AETHERIS_HTML_TOKENIZER_HPP
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aetheris::rendering {

enum class HtmlTokenType : uint8_t {
    Doctype,
    StartTag,
    EndTag,
    SelfClosingTag,
    Text,
    Comment,
    EOFToken,
};

struct HtmlToken {
    HtmlTokenType type { HtmlTokenType::EOFToken };
    std::string data;
    std::unordered_map<std::string, std::string> attributes;
    size_t line { 1 };
    size_t column { 1 };
    bool is_self_closing { false };
};

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
    BogusComment,
};

class HtmlTokenizer {
public:
    explicit HtmlTokenizer(std::string_view input);

    HtmlToken next_token();
    bool eof() const { return m_position >= m_input.size(); }

    size_t line() const { return m_line; }
    size_t column() const { return m_column; }

private:
    char peek(size_t offset = 0) const;
    bool starts_with(std::string_view text) const;
    void consume();
    void skip_whitespace();
    void advance_text(std::string_view text);

    HtmlToken make_token(HtmlTokenType type, std::string data, size_t line, size_t column) const;
    HtmlToken parse_tag(bool is_end_tag, size_t token_line, size_t token_column);
    HtmlToken parse_comment(size_t token_line, size_t token_column);
    HtmlToken parse_doctype(size_t token_line, size_t token_column);
    HtmlToken parse_text();

    static bool is_whitespace(char c);
    static bool is_alpha(char c);
    static bool is_attribute_name_char(char c);
    static char to_lower(char c);
    static std::string to_lower_copy(std::string_view text);

    std::string_view m_input;
    size_t m_position { 0 };
    size_t m_line { 1 };
    size_t m_column { 1 };
    bool m_emitted_eof { false };
};

} // namespace aetheris::rendering

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace aetheris::rendering {

enum class CssTokenType {
    Identifier,
    String,
    Number,
    Colon,
    Semicolon,
    Comma,
    OpenBrace,
    CloseBrace,
    OpenParen,
    CloseParen,
    Delimiter,
    Whitespace,
    EndOfFile,
};

struct CssToken {
    CssTokenType type { CssTokenType::EndOfFile };
    std::string value;
    size_t line { 1 };
    size_t column { 1 };
};

class CssTokenizer {
public:
    explicit CssTokenizer(std::string_view input);

    std::vector<CssToken> tokenize();

private:
    bool eof() const;
    char peek(size_t offset = 0) const;
    void consume();
    void skip_comment();
    CssToken make_token(CssTokenType type, std::string value, size_t line, size_t column) const;
    CssToken consume_identifier();
    CssToken consume_number();
    CssToken consume_string(char quote);

    static bool is_whitespace(char c);
    static bool is_identifier_start(char c);
    static bool is_identifier_char(char c);

    std::string_view m_input;
    size_t m_position { 0 };
    size_t m_line { 1 };
    size_t m_column { 1 };
};

} // namespace aetheris::rendering

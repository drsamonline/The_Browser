#pragma once

#include "css_stylesheet.hpp"
#include "css_tokenizer.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace aetheris::rendering {

class CssParser {
public:
    CssStyleSheet parse(std::string_view input);

private:
    CssStyleSheet parse_tokens(std::vector<CssToken> const& tokens);
    // Parses rules until the matching close brace (or EOF) and appends them
    // when `include` is true. Used for @media blocks, which nest rules.
    void parse_block(std::vector<CssToken> const& tokens, size_t& position, CssStyleSheet& sheet,
        size_t& source_order, bool include);
    // True when a media query matches the renderer's viewport (screen, 1024x768).
    static bool media_matches(std::string const& query);
    static void skip_block(std::vector<CssToken> const& tokens, size_t& position);
    static std::string trim(std::string value);
    static std::string join_value(std::vector<CssToken> const& tokens, size_t begin, size_t end);
    static bool is_significant(CssToken const& token);
};

} // namespace aetheris::rendering

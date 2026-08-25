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
    static std::string trim(std::string value);
    static std::string join_value(std::vector<CssToken> const& tokens, size_t begin, size_t end);
    static bool is_significant(CssToken const& token);
};

} // namespace aetheris::rendering

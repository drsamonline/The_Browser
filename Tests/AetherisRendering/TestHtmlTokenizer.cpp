#include <html_tokenizer.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace aetheris::rendering;

static void require(bool condition, std::string const& message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

static std::vector<HtmlToken> tokenize(std::string_view input)
{
    HtmlTokenizer tokenizer(input);
    std::vector<HtmlToken> tokens;

    for (;;) {
        auto token = tokenizer.next_token();
        tokens.push_back(token);
        if (token.type == HtmlTokenType::EOFToken)
            break;
    }
    return tokens;
}

int main()
{
    {
        auto tokens = tokenize("Hello");
        require(tokens.size() == 2, "plain text should produce text + EOF");
        require(tokens[0].type == HtmlTokenType::Text, "plain text token type");
        require(tokens[0].data == "Hello", "plain text value");
    }

    {
        auto tokens = tokenize("<DIV id=\"main\" disabled>Text</DIV>");
        require(tokens.size() == 4, "element should produce start, text, end, EOF");
        require(tokens[0].type == HtmlTokenType::StartTag, "start tag type");
        require(tokens[0].data == "div", "tag names should normalize to lowercase");
        require(tokens[0].attributes.at("id") == "main", "quoted attribute");
        require(tokens[0].attributes.at("disabled").empty(), "boolean attribute");
        require(tokens[1].data == "Text", "element text");
        require(tokens[2].type == HtmlTokenType::EndTag, "end tag type");
        require(tokens[2].data == "div", "end tag name");
    }

    {
        auto tokens = tokenize("<img src=test.png alt='Sample'/>");
        require(tokens[0].type == HtmlTokenType::SelfClosingTag, "self-closing tag type");
        require(tokens[0].is_self_closing, "self-closing flag");
        require(tokens[0].attributes.at("src") == "test.png", "unquoted attribute");
        require(tokens[0].attributes.at("alt") == "Sample", "single-quoted attribute");
    }

    {
        auto tokens = tokenize("<!-- note -->");
        require(tokens[0].type == HtmlTokenType::Comment, "comment token type");
        require(tokens[0].data == " note ", "comment data");
    }

    {
        auto tokens = tokenize("<!DOCTYPE html>");
        require(tokens[0].type == HtmlTokenType::Doctype, "doctype token type");
        require(tokens[0].data == "html", "doctype data");
    }

    {
        auto tokens = tokenize("a < b");
        require(tokens[0].data == "a ", "text before malformed tag");
        require(tokens[1].data == "<", "malformed tag opener remains text");
        require(tokens[2].data == " b", "text after malformed tag opener");
    }

    return 0;
}

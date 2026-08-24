#include "html_tree_builder.hpp"

#include <algorithm>

namespace aetheris::rendering {

std::unique_ptr<DomNode> HtmlTreeBuilder::build(std::string_view input)
{
    auto document = std::make_unique<DomNode>(DomNodeType::Document);
    std::vector<DomNode*> open_elements { document.get() };
    HtmlTokenizer tokenizer(input);

    for (;;) {
        auto token = tokenizer.next_token();
        if (token.type == HtmlTokenType::EOFToken)
            break;
        insert_token(*document, open_elements, token);
    }

    return document;
}

void HtmlTreeBuilder::insert_token(DomNode& document, std::vector<DomNode*>& open_elements, HtmlToken const& token)
{
    auto* current = open_elements.empty() ? &document : open_elements.back();

    switch (token.type) {
    case HtmlTokenType::Doctype:
        insert_node(*current, std::make_unique<DomNode>(DomNodeType::Doctype, {}, token.data), open_elements, false);
        break;
    case HtmlTokenType::Comment:
        insert_node(*current, std::make_unique<DomNode>(DomNodeType::Comment, {}, token.data), open_elements, false);
        break;
    case HtmlTokenType::Text:
        if (!token.data.empty())
            insert_node(*current, std::make_unique<DomNode>(DomNodeType::Text, {}, token.data), open_elements, false);
        break;
    case HtmlTokenType::StartTag:
    case HtmlTokenType::SelfClosingTag: {
        auto node = std::make_unique<DomNode>(DomNodeType::Element, token.data);
        node->attributes = token.attributes;
        bool push_to_stack = token.type == HtmlTokenType::StartTag && !is_void_element(token.data);
        insert_node(*current, std::move(node), open_elements, push_to_stack);
        break;
    }
    case HtmlTokenType::EndTag:
        close_element(open_elements, token.data);
        break;
    case HtmlTokenType::EOFToken:
        break;
    }
}

void HtmlTreeBuilder::insert_node(DomNode& parent, std::unique_ptr<DomNode> node, std::vector<DomNode*>& open_elements, bool push_to_stack)
{
    auto& inserted = parent.append_child(std::move(node));
    if (push_to_stack)
        open_elements.push_back(&inserted);
}

void HtmlTreeBuilder::close_element(std::vector<DomNode*>& open_elements, std::string_view name)
{
    for (size_t i = open_elements.size(); i > 1; --i) {
        if (open_elements[i - 1]->name == name) {
            open_elements.resize(i - 1);
            return;
        }
    }
}

bool HtmlTreeBuilder::is_void_element(std::string_view name)
{
    static constexpr std::string_view void_elements[] = {
        "area", "base", "br", "col", "embed", "hr", "img",
        "input", "link", "meta", "param", "source", "track", "wbr"
    };

    return std::find(std::begin(void_elements), std::end(void_elements), name) != std::end(void_elements);
}

} // namespace aetheris::rendering

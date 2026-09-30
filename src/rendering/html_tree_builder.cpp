#include "html_tree_builder.hpp"

#include <algorithm>

namespace aetheris::rendering {

namespace {

bool is_html_document_element(std::string_view name)
{
    static constexpr std::string_view document_elements[] = {
        "address", "article", "aside", "blockquote", "body", "button", "canvas",
        "dd", "details", "div", "dl", "dt", "embed", "fieldset", "figcaption",
        "figure", "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6", "header",
        "hr", "html", "iframe", "img", "input", "label", "li", "main", "menu",
        "nav", "noscript", "ol", "p", "pre", "script", "section", "select",
        "source", "style", "table", "template", "textarea", "ul", "video"
    };
    return std::find(std::begin(document_elements), std::end(document_elements), name)
        != std::end(document_elements);
}

bool contains_html_document_element(DomNode const& node)
{
    for (auto const& child : node.children) {
        if (child->type == DomNodeType::Element && is_html_document_element(child->name))
            return true;
    }
    return false;
}

void reparent_children(DomNode& from, DomNode& to)
{
    for (auto& child : from.children) {
        child->parent = &to;
        to.children.push_back(std::move(child));
    }
    from.children.clear();
}

} // namespace

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

    synthesize_document_structure(*document);

    return document;
}

void HtmlTreeBuilder::synthesize_document_structure(DomNode& document)
{
    // Find an existing <html> element among the top-level children.
    DomNode* html_element = nullptr;
    for (auto& child : document.children) {
        if (child->type == DomNodeType::Element && child->name == "html") {
            html_element = child.get();
            break;
        }
    }

    if (!html_element) {
        if (!contains_html_document_element(document))
            return;

        // Wrap all element content in implicit <html> and <body> elements,
        // leaving comments, doctypes and stray text at the document level.
        auto wrapper = std::make_unique<DomNode>(DomNodeType::Document);
        reparent_children(document, *wrapper);

        auto html = std::make_unique<DomNode>(DomNodeType::Element, "html");
        auto body = std::make_unique<DomNode>(DomNodeType::Element, "body");

        for (auto& child : wrapper->children) {
            if (child->type == DomNodeType::Element) {
                child->parent = body.get();
                body->children.push_back(std::move(child));
            } else if (child->type == DomNodeType::Text && !child->data.empty()) {
                child->parent = body.get();
                body->children.push_back(std::move(child));
            } else {
                child->parent = &document;
                document.children.push_back(std::move(child));
            }
        }

        body->parent = html.get();
        html->children.push_back(std::move(body));
        html->parent = &document;
        document.children.push_back(std::move(html));
        return;
    }

    // The document already has <html>; make sure it has a <body>.
    DomNode* body_element = nullptr;
    for (auto& child : html_element->children) {
        if (child->type == DomNodeType::Element && child->name == "body") {
            body_element = child.get();
            break;
        }
    }

    if (body_element)
        return;

    auto body = std::make_unique<DomNode>(DomNodeType::Element, "body");
    auto wrapper = std::make_unique<DomNode>(DomNodeType::Document);
    reparent_children(*html_element, *wrapper);

    for (auto& child : wrapper->children) {
        if (child->type == DomNodeType::Element || child->type == DomNodeType::Text) {
            child->parent = body.get();
            body->children.push_back(std::move(child));
        } else {
            child->parent = html_element;
            html_element->children.push_back(std::move(child));
        }
    }

    body->parent = html_element;
    html_element->children.push_back(std::move(body));
}

void HtmlTreeBuilder::insert_token(DomNode& document, std::vector<DomNode*>& open_elements, HtmlToken const& token)
{
    auto* current = open_elements.empty() ? &document : open_elements.back();

    switch (token.type) {
    case HtmlTokenType::Doctype:
        insert_node(*current, std::make_unique<DomNode>(DomNodeType::Doctype, std::string {}, token.data), open_elements, false);
        break;
    case HtmlTokenType::Comment:
        insert_node(*current, std::make_unique<DomNode>(DomNodeType::Comment, std::string {}, token.data), open_elements, false);
        break;
    case HtmlTokenType::Text:
        if (!token.data.empty())
            insert_node(*current, std::make_unique<DomNode>(DomNodeType::Text, std::string {}, token.data), open_elements, false);
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

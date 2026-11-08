#include "document.hpp"

#include <gumbo.h>

#include <algorithm>
#include <cctype>

#include "html_tree_builder.hpp"

namespace aetheris::rendering {

namespace {

std::string lower_copy(std::string_view text)
{
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

std::unique_ptr<DomNode> convert_gumbo_node(GumboNode* node)
{
    switch (node->type) {
    case GUMBO_NODE_DOCUMENT: {
        auto root = std::make_unique<DomNode>(DomNodeType::Document);
        auto* children = &node->v.document.children;
        for (unsigned i = 0; i < children->length; ++i)
            if (auto child = convert_gumbo_node(static_cast<GumboNode*>(children->data[i])))
                root->append_child(std::move(child));
        return root;
    }
    case GUMBO_NODE_ELEMENT: {
        auto& element = node->v.element;
        std::string name = gumbo_normalized_tagname(element.tag);
        if (name.empty() || element.tag == GUMBO_TAG_UNKNOWN)
            name = lower_copy(std::string_view(element.original_tag.data, element.original_tag.length));
        auto result = std::make_unique<DomNode>(DomNodeType::Element, name);
        for (unsigned i = 0; i < element.attributes.length; ++i) {
            auto* attribute = static_cast<GumboAttribute*>(element.attributes.data[i]);
            result->attributes[attribute->name] = attribute->value;
        }
        for (unsigned i = 0; i < element.children.length; ++i)
            if (auto child = convert_gumbo_node(static_cast<GumboNode*>(element.children.data[i])))
                result->append_child(std::move(child));
        return result;
    }
    case GUMBO_NODE_TEXT:
    case GUMBO_NODE_CDATA:
    case GUMBO_NODE_WHITESPACE:
        return std::make_unique<DomNode>(DomNodeType::Text, "", node->v.text.text);
    case GUMBO_NODE_COMMENT:
        return std::make_unique<DomNode>(DomNodeType::Comment, "", node->v.text.text);
    default:
        return nullptr;
    }
}

// The HTML5 algorithm wraps fragment input in an implicit <html>/<head>/<body>.
// This renderer treats parsed content as authored, so those wrappers are
// dropped when the document did not author an <html> element itself.
std::unique_ptr<DomNode> strip_synthetic_wrappers(std::unique_ptr<DomNode> root)
{
    bool has_html = false;
    for (auto& child : root->children)
        if (child->type == DomNodeType::Element && child->name == "html")
            has_html = true;
    if (!has_html)
        return root;

    // Splice the <html> wrapper out in place, flattening <head>/<body>, so that
    // siblings such as leading comments keep their position. Head content is
    // kept: a fragment like "<link ...><img ...>" has its <link>s moved into
    // head by the parser, and dropping head would lose them.
    auto flattened = std::make_unique<DomNode>(DomNodeType::Document);
    auto splice = [&flattened](DomNode& node) {
        for (auto& child : node.children)
            flattened->append_child(std::move(child));
    };
    for (auto& child : root->children) {
        if (child->type == DomNodeType::Element && child->name == "html") {
            for (auto& inner : child->children) {
                bool is_wrapper = inner->type == DomNodeType::Element
                    && (inner->name == "head" || inner->name == "body");
                if (is_wrapper)
                    splice(*inner);
                else
                    flattened->append_child(std::move(inner));
            }
        } else {
            flattened->append_child(std::move(child));
        }
    }
    return flattened;
}

} // namespace


Document::Document(std::unique_ptr<DomNode> root)
    : m_root(std::move(root))
{
}

Document Document::parse_html(std::string_view input)
{
    // Parse with gumbo (the HTML5 algorithm), then convert to the internal tree.
    std::string html(input);
    GumboOutput* output = gumbo_parse_with_options(&kGumboDefaultOptions, html.data(), html.size());
    if (!output) {
        HtmlTreeBuilder builder;
        return Document(builder.build(input));
    }

    // GumboOutput has both `document` (the document node) and `root` (the root
    // element); convert the document so the tree keeps its top-level node.
    auto root = convert_gumbo_node(output->document);
    gumbo_destroy_output(&kGumboDefaultOptions, output);

    auto lowered = lower_copy(input);
    bool authored_html = lowered.find("<html") != std::string::npos;
    bool authored_head = lowered.find("<head") != std::string::npos;
    if (!authored_html) {
        root = strip_synthetic_wrappers(std::move(root));
    } else if (!authored_head) {
        // The parser always inserts a <head>; drop it when it is empty and the
        // document did not author one, so the tree stays close to the source.
        for (auto& child : root->children) {
            if (child->type != DomNodeType::Element || child->name != "html")
                continue;
            for (auto it = child->children.begin(); it != child->children.end();) {
                if ((*it)->type == DomNodeType::Element && (*it)->name == "head" && (*it)->children.empty())
                    it = child->children.erase(it);
                else
                    ++it;
            }
        }
    }

    return Document(std::move(root));
}

} // namespace aetheris::rendering

#include "document.hpp"

#include "html_tree_builder.hpp"

namespace aetheris::rendering {

Document::Document(std::unique_ptr<DomNode> root)
    : m_root(std::move(root))
{
}

Document Document::parse_html(std::string_view input)
{
    HtmlTreeBuilder builder;
    return Document(builder.build(input));
}

} // namespace aetheris::rendering

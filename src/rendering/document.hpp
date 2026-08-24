#pragma once

#include "dom.hpp"

#include <memory>
#include <string_view>

namespace aetheris::rendering {

class Document {
public:
    static Document parse_html(std::string_view input);

    DomNode& root() { return *m_root; }
    DomNode const& root() const { return *m_root; }

private:
    explicit Document(std::unique_ptr<DomNode> root);

    std::unique_ptr<DomNode> m_root;
};

} // namespace aetheris::rendering

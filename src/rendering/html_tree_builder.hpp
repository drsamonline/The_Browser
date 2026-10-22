#pragma once

#include "dom.hpp"
#include "html_tokenizer.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace aetheris::rendering {

class HtmlTreeBuilder {
public:
    std::unique_ptr<DomNode> build(std::string_view input);

private:
    void insert_token(DomNode& document, std::vector<DomNode*>& open_elements, HtmlToken const& token);
    void insert_node(DomNode& parent, std::unique_ptr<DomNode> node, std::vector<DomNode*>& open_elements, bool push_to_stack);
    static void close_element(std::vector<DomNode*>& open_elements, std::string_view name);
    static bool is_void_element(std::string_view name);
};

} // namespace aetheris::rendering

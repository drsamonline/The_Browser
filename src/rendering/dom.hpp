#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace aetheris::rendering {

enum class DomNodeType {
    Document,
    Element,
    Text,
    Comment,
    Doctype,
};

struct DomNode {
    DomNodeType type { DomNodeType::Document };
    std::string name;
    std::string data;
    std::unordered_map<std::string, std::string> attributes;
    std::vector<std::unique_ptr<DomNode>> children;
    DomNode* parent { nullptr };

    DomNode() = default;
    DomNode(DomNodeType node_type, std::string node_name = {}, std::string node_data = {});

    DomNode& append_child(std::unique_ptr<DomNode> child);
    DomNode* first_child() const;
    DomNode* last_child() const;

    std::string const* attribute(std::string const& name) const;
    bool has_attribute(std::string const& name) const;
};

} // namespace aetheris::rendering

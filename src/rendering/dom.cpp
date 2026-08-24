#include "dom.hpp"

namespace aetheris::rendering {

DomNode::DomNode(DomNodeType node_type, std::string node_name, std::string node_data)
    : type(node_type)
    , name(std::move(node_name))
    , data(std::move(node_data))
{
}

DomNode& DomNode::append_child(std::unique_ptr<DomNode> child)
{
    child->parent = this;
    children.push_back(std::move(child));
    return *children.back();
}

DomNode* DomNode::first_child() const
{
    return children.empty() ? nullptr : children.front().get();
}

DomNode* DomNode::last_child() const
{
    return children.empty() ? nullptr : children.back().get();
}

std::string const* DomNode::attribute(std::string const& attribute_name) const
{
    auto it = attributes.find(attribute_name);
    return it == attributes.end() ? nullptr : &it->second;
}

bool DomNode::has_attribute(std::string const& attribute_name) const
{
    return attributes.contains(attribute_name);
}

} // namespace aetheris::rendering

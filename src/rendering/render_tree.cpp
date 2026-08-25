#include "render_tree.hpp"

namespace aetheris::rendering {

RenderTree RenderTree::from_layout(LayoutNode const& root)
{
    RenderTree tree;
    emit(root, tree.m_commands);
    return tree;
}

void RenderTree::emit(LayoutNode const& node, std::vector<PaintCommand>& commands)
{
    if (node.display == LayoutDisplay::None)
        return;

    if (node.dom_node && node.dom_node->type == DomNodeType::Text) {
        PaintCommand command;
        command.type = PaintCommand::Type::DrawText;
        command.rect = node.rect;
        command.text = node.dom_node->data;
        if (auto color = node.style.get("color"))
            command.color = *color;
        commands.push_back(std::move(command));
    } else if (node.dom_node && node.dom_node->type == DomNodeType::Element) {
        if (auto background = node.style.get("background-color")) {
            PaintCommand command;
            command.type = PaintCommand::Type::FillRect;
            command.rect = node.rect;
            command.color = *background;
            commands.push_back(std::move(command));
        }
    }

    for (auto const& child : node.children)
        emit(*child, commands);
}

} // namespace aetheris::rendering

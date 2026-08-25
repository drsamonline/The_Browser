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

    bool clips_children = node.style.get("overflow") && *node.style.get("overflow") == "hidden";
    if (clips_children) {
        PaintCommand command;
        command.rect = node.box.content;
        command.push_clip = true;
        commands.push_back(std::move(command));
    }

    if (node.dom_node && node.dom_node->type == DomNodeType::Text) {
        for (auto const& fragment : node.text_fragments) {
            PaintCommand command;
            command.type = PaintCommand::Type::DrawText;
            command.rect = fragment.rect;
            command.baseline = fragment.baseline;
            command.text = fragment.text;
            if (auto color = node.style.get("color"))
                command.color = *color;
            commands.push_back(std::move(command));
        }
    } else if (node.dom_node && node.dom_node->type == DomNodeType::Element) {
        if (auto background = node.style.get("background-color")) {
            PaintCommand command;
            command.type = PaintCommand::Type::FillRect;
            command.rect = node.rect;
            command.color = *background;
            commands.push_back(std::move(command));
        }

        if (node.box.border.top > 0 || node.box.border.right > 0 || node.box.border.bottom > 0 || node.box.border.left > 0) {
            PaintCommand command;
            command.type = PaintCommand::Type::StrokeRect;
            command.rect = node.rect;
            command.edges = node.box.border;
            command.color = node.style.get("border-color") ? *node.style.get("border-color") : "black";
            commands.push_back(std::move(command));
        }
    }

    for (auto const& child : node.children)
        emit(*child, commands);

    if (clips_children) {
        PaintCommand command;
        command.pop_clip = true;
        commands.push_back(std::move(command));
    }
}

} // namespace aetheris::rendering

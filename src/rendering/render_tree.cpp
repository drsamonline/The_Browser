#include "render_tree.hpp"

#include <cstdlib>

namespace aetheris::rendering {

namespace {
float number(std::string const* value, float fallback)
{
    if (!value)
        return fallback;
    char* end = nullptr;
    auto result = std::strtof(value->c_str(), &end);
    return end == value->c_str() ? fallback : result;
}
}

RenderTree RenderTree::from_layout(LayoutNode const& root)
{
    RenderTree tree;
    emit(root, tree.m_commands, 1.0f);
    return tree;
}

void RenderTree::emit(LayoutNode const& node, std::vector<PaintCommand>& commands, float inherited_opacity)
{
    if (node.display == LayoutDisplay::None)
        return;

    float opacity = std::clamp(inherited_opacity * number(node.style.get("opacity"), 1.0f), 0.0f, 1.0f);
    float radius = std::max(0.0f, number(node.style.get("border-radius"), 0.0f));

    bool clips_children = node.style.get("overflow") && (*node.style.get("overflow") == "hidden" || *node.style.get("overflow") == "clip");
    if (clips_children) {
        PaintCommand command;
        command.rect = node.box.content;
        command.radius = radius;
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
            command.opacity = opacity;
            if (auto color = node.style.get("color"))
                command.color = *color;
            commands.push_back(std::move(command));
        }
    } else if (node.dom_node && node.dom_node->type == DomNodeType::Element) {
        if (node.dom_node->name == "img" && node.image) { PaintCommand command; command.type = PaintCommand::Type::DrawImage; command.rect = node.box.content; command.image = node.image; command.opacity = opacity; commands.push_back(std::move(command)); }
        if (auto background = node.style.get("background-color")) {
            PaintCommand command;
            command.type = radius > 0 ? PaintCommand::Type::FillRoundedRect : PaintCommand::Type::FillRect;
            command.rect = node.rect;
            command.radius = radius;
            command.color = *background;
            command.opacity = opacity;
            commands.push_back(std::move(command));
        }

        if (node.box.border.top > 0 || node.box.border.right > 0 || node.box.border.bottom > 0 || node.box.border.left > 0) {
            PaintCommand command;
            command.type = radius > 0 ? PaintCommand::Type::StrokeRoundedRect : PaintCommand::Type::StrokeRect;
            command.rect = node.rect;
            command.edges = node.box.border;
            command.radius = radius;
            command.opacity = opacity;
            command.border_style = node.style.get("border-style") ? *node.style.get("border-style") : "solid";
            command.color = node.style.get("border-color") ? *node.style.get("border-color") : "black";
            commands.push_back(std::move(command));
        }
    }

    for (auto const& child : node.children)
        emit(*child, commands, opacity);

    if (clips_children) {
        PaintCommand command;
        command.pop_clip = true;
        commands.push_back(std::move(command));
    }
}

} // namespace aetheris::rendering

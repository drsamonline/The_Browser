#include "render_tree.hpp"

#include <cstdlib>
#include <algorithm>
#include <sstream>

namespace aetheris::rendering {

namespace {
int z_index(LayoutNode const& node) { auto value=node.style.get("z-index"); if (!value || *value=="auto") return 0; char* end=nullptr; long parsed=std::strtol(value->c_str(), &end, 10); return end==value->c_str()?0:static_cast<int>(parsed); }

float number(std::string const* value, float fallback)
{
    if (!value)
        return fallback;
    char* end = nullptr;
    auto result = std::strtof(value->c_str(), &end);
    return end == value->c_str() ? fallback : result;
}
}

namespace {

// Parses the transform forms this renderer supports: translate/translateX/
// translateY and scale. Anything else (rotate, skew, matrix) is ignored.
PaintTransform parse_transform(std::string const* value)
{
    PaintTransform transform;
    if (!value)
        return transform;

    auto number_after = [](std::string const& text, std::size_t start, bool& ok) {
        auto end = text.find_first_of(",)", start);
        std::string piece = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        piece.erase(std::remove_if(piece.begin(), piece.end(), [](char c) {
            return std::isalpha(static_cast<unsigned char>(c)) || std::isspace(static_cast<unsigned char>(c));
        }), piece.end());
        if (piece.empty()) {
            ok = false;
            return 0.0f;
        }
        char* tail = nullptr;
        float result = std::strtof(piece.c_str(), &tail);
        ok = tail != piece.c_str();
        return result;
    };

    std::size_t position = 0;
    while (position < value->size()) {
        auto open = value->find('(', position);
        if (open == std::string::npos)
            break;
        auto close = value->find(')', open);
        if (close == std::string::npos)
            break;
        std::string name = value->substr(position, open - position);
        name.erase(std::remove_if(name.begin(), name.end(), [](char c) {
            return std::isspace(static_cast<unsigned char>(c));
        }), name.end());

        std::string arguments = value->substr(open + 1, close - open - 1);
        bool ok = true;
        if (name == "translate") {
            transform.tx += number_after(arguments, 0, ok);
            auto comma = arguments.find(',');
            if (comma != std::string::npos)
                transform.ty += number_after(arguments, comma + 1, ok);
        } else if (name == "translateX") {
            transform.tx += number_after(arguments, 0, ok);
        } else if (name == "translateY") {
            transform.ty += number_after(arguments, 0, ok);
        } else if (name == "scale") {
            bool ok_x = true;
            float sx = number_after(arguments, 0, ok_x);
            auto comma = arguments.find(',');
            if (comma != std::string::npos) {
                bool ok_y = true;
                float sy = number_after(arguments, comma + 1, ok_y);
                if (ok_x && ok_y) {
                    transform.sx *= sx;
                    transform.sy *= sy;
                }
            } else if (ok_x) {
                transform.sx *= sx;
                transform.sy *= sx;
            }
        }
        position = close + 1;
    }
    return transform;
}

void apply_transform(PaintTransform const& transform, PaintCommand& command)
{
    if (transform.sx == 1.0f && transform.sy == 1.0f && transform.tx == 0.0f && transform.ty == 0.0f)
        return;
    command.rect.x = command.rect.x * transform.sx + transform.tx;
    command.rect.y = command.rect.y * transform.sy + transform.ty;
    command.rect.width *= transform.sx;
    command.rect.height *= transform.sy;
    command.baseline *= transform.sy;
}

} // namespace

RenderTree RenderTree::from_layout(LayoutNode const& root)
{
    RenderTree tree;
    emit(root, tree.m_commands, 1.0f, PaintTransform {});
    return tree;
}

void RenderTree::emit(LayoutNode const& node, std::vector<PaintCommand>& commands, float inherited_opacity,
    PaintTransform const& inherited)
{
    if (node.display == LayoutDisplay::None)
        return;

    // Compose this node's transform with the inherited one. A point is mapped
    // by the local transform first, then by the ancestor's.
    PaintTransform local = parse_transform(node.style.get("transform"));
    PaintTransform cumulative;
    cumulative.sx = inherited.sx * local.sx;
    cumulative.sy = inherited.sy * local.sy;
    cumulative.tx = inherited.sx * local.tx + inherited.tx;
    cumulative.ty = inherited.sy * local.ty + inherited.ty;
    std::size_t own_start = commands.size();

    float opacity = std::clamp(inherited_opacity * number(node.style.get("opacity"), 1.0f), 0.0f, 1.0f);
    float radius = std::max(0.0f, number(node.style.get("border-radius"), 0.0f));

    bool clips_children = node.style.get("overflow") && (*node.style.get("overflow") == "hidden" || *node.style.get("overflow") == "clip");

    if (node.dom_node && node.dom_node->type == DomNodeType::Text) {
        for (auto const& fragment : node.text_fragments) {
            PaintCommand command;
            command.type = PaintCommand::Type::DrawText;
            command.rect = fragment.rect;
            command.baseline = fragment.baseline;
            command.text = fragment.text;
            command.opacity = opacity;
            command.font_size = number(node.style.get("font-size"), 16.0f);
            if (auto color = node.style.get("color"))
                command.color = *color;
            commands.push_back(command);
            if (auto decoration = node.style.get("text-decoration"); decoration && *decoration != "none") {
                PaintCommand decoration_command;
                decoration_command.type = PaintCommand::Type::DrawTextDecoration;
                decoration_command.rect = fragment.rect;
                decoration_command.text_decoration = *decoration;
                decoration_command.opacity = opacity;
                decoration_command.color = command.color;
                commands.push_back(std::move(decoration_command));
            }
        }
    } else if (node.dom_node && node.dom_node->type == DomNodeType::Element) {
        if (auto shadow = node.style.get("box-shadow")) { PaintCommand command; command.type=PaintCommand::Type::DrawShadow; command.rect=node.rect; command.opacity=opacity; std::istringstream stream(*shadow); std::string x,y,blur,color; stream>>x>>y>>blur>>color; command.shadow_offset_x=number(&x,0); command.shadow_offset_y=number(&y,0); command.shadow_blur=number(&blur,0); command.color=color.empty()?"black":color; commands.push_back(std::move(command)); }
        if (node.background_image) { PaintCommand command; command.type=PaintCommand::Type::DrawImage; command.rect=node.box.content; command.image=node.background_image; command.image_fit="fill"; command.background_repeat=node.style.get("background-repeat") ? *node.style.get("background-repeat") : "repeat"; command.background_position=node.style.get("background-position") ? *node.style.get("background-position") : "0 0"; command.background_size=node.style.get("background-size") ? *node.style.get("background-size") : "auto"; command.opacity=opacity; commands.push_back(std::move(command)); }
        if (node.dom_node->name == "img" && node.image) { PaintCommand command; command.type = PaintCommand::Type::DrawImage; command.rect = node.box.content; command.image = node.image; command.image_fit = node.style.get("object-fit") ? *node.style.get("object-fit") : "fill"; command.opacity = opacity; commands.push_back(std::move(command)); }
        if (auto outline=node.style.get("outline")) { std::istringstream stream(*outline); std::string width,style,color; stream>>width>>style>>color; PaintCommand command; command.type=PaintCommand::Type::DrawOutline; command.rect=node.rect; float w=number(&width,0); command.edges={w,w,w,w}; command.border_style=style.empty()?"solid":style; command.color=color.empty()?"black":color; command.opacity=opacity; commands.push_back(std::move(command)); }
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

    if (clips_children) {
        // The overflow clip applies to descendants only; the element's own
        // background and border were painted above and must remain visible,
        // so push the clip here (after self-painting) rather than at entry.
        PaintCommand command;
        command.rect = {
            node.box.content.x - node.box.padding.left,
            node.box.content.y - node.box.padding.top,
            node.box.content.width + node.box.padding.left + node.box.padding.right,
            node.box.content.height + node.box.padding.top + node.box.padding.bottom
        };
        command.radius = radius;
        command.push_clip = true;
        commands.push_back(std::move(command));
    }

    std::vector<LayoutNode const*> ordered_children;
    ordered_children.reserve(node.children.size());
    bool any_z_index = false;
    // This node's own commands carry the cumulative transform; children compose
    // it again with their own.
    for (std::size_t index = own_start; index < commands.size(); ++index)
        apply_transform(cumulative, commands[index]);

    for (auto const& child : node.children) {
        if (z_index(*child) != 0)
            any_z_index = true;
        ordered_children.push_back(child.get());
    }
    // Only pay for a stable sort when a child actually sets a z-index; the
    // common case is that every child is auto (0) and the order is unchanged.
    if (any_z_index) {
        std::stable_sort(ordered_children.begin(), ordered_children.end(), [](auto const* a, auto const* b) {
            return z_index(*a) < z_index(*b);
        });
    }
    for (auto const* child : ordered_children)
        emit(*child, commands, opacity, cumulative);

    if (clips_children) {
        std::size_t tail_start = commands.size();
        PaintCommand command;
        command.pop_clip = true;
        commands.push_back(std::move(command));
        // The pop belongs to this node, so it carries this node's transform.
        for (std::size_t index = tail_start; index < commands.size(); ++index)
            apply_transform(cumulative, commands[index]);
    }
}

} // namespace aetheris::rendering

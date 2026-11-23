#include "render_tree.hpp"
#include <cstdio>
#include <cstdlib>

#include "css_units.hpp"

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

RenderTree RenderTree::from_layout(LayoutNode const& root, std::unordered_map<LayoutNode const*, float> const* opacity_overrides)
{
    RenderTree tree;
    emit(root, tree.m_commands, 1.0f, PaintTransform {}, tree.m_sticky_regions, opacity_overrides);
    return tree;
}

void RenderTree::emit(LayoutNode const& node, std::vector<PaintCommand>& commands, float inherited_opacity,
    PaintTransform const& inherited, std::vector<StickyRegion>& sticky_regions,
    std::unordered_map<LayoutNode const*, float> const* opacity_overrides)
{
    if (node.display == LayoutDisplay::None)
        return;

    // `visibility` is inherited and checked per box, which is upstream's rule
    // (Painting/BoxViews.cpp:189: a box is painted when visibility == Visible && opacity != 0). The
    // difference from display:none is the whole point: the box still occupies space, and a descendant
    // that sets `visibility: visible` is painted inside a hidden ancestor. So this skips the node's own
    // commands and still walks the children.
    bool const visible = [&] {
        auto const* value = node.style.get("visibility");
        return !value || (*value != "hidden" && *value != "collapse");
    }();
    if (!visible) {
        float const opacity = std::clamp(inherited_opacity * number(node.style.get("opacity"), 1.0f), 0.0f, 1.0f);
        for (auto const& child : node.children)
            emit(*child, commands, opacity, inherited, sticky_regions, opacity_overrides);
        return;
    }

    // Compose this node's transform with the inherited one. A point is mapped
    // by the local transform first, then by the ancestor's.
    PaintTransform local = parse_transform(node.style.get("transform"));
    PaintTransform cumulative;
    cumulative.sx = inherited.sx * local.sx;
    cumulative.sy = inherited.sy * local.sy;
    cumulative.tx = inherited.sx * local.tx + inherited.tx;
    cumulative.ty = inherited.sy * local.ty + inherited.ty;
    std::size_t own_start = commands.size();

    // `position: sticky` is in normal flow, so its geometry is already right; what it needs is a paint-time
    // shift that keeps it inside the scrollport, clamped to its containing block (row 3b).
    auto const* position = node.style.get("position");
    bool const is_sticky = position && *position == "sticky";

    float own_opacity = number(node.style.get("opacity"), 1.0f);
    if (opacity_overrides) {
        if (auto found = opacity_overrides->find(&node); found != opacity_overrides->end())
            own_opacity = found->second;
    }
    float opacity = std::clamp(inherited_opacity * own_opacity, 0.0f, 1.0f);
    float radius = std::max(0.0f, number(node.style.get("border-radius"), 0.0f));

    bool clips_children = node.style.get("overflow") && (*node.style.get("overflow") == "hidden" || *node.style.get("overflow") == "clip");

    // A node with text fragments is a text run, whether or not a DOM node produced it: a box
    // generated for `::before`/`::after` has no DOM node and still paints. Upstream's generated node
    // carries its text and its generator element, not a text node (Layout/Node.h,
    // `pseudo_element_generator`).
    if (std::getenv("AETHERIS_TRACE_GENERATED") && node.is_generated)
        std::fprintf(stderr, "PAINT gen=[%s] frags=%zu rect=(%.1f,%.1f %.1fx%.1f) color=[%s]\n",
            node.generated_text.c_str(), node.text_fragments.size(),
            node.rect.x, node.rect.y, node.rect.width, node.rect.height,
            node.style.get("color") ? node.style.get("color")->c_str() : "<none>");
    // The box paints before its text: a background, border, shadow or outline belongs behind the glyphs.
    // This used to be an `else` of the text branch, which meant a generated box - the one node kind that
    // carries its own text and its own background - painted whichever came first in that condition and lost
    // the other (row 3a3).
    auto paint_box = [&] {
        // A generated box paints its background, shadow and outline like any other box. It has no DOM
        // node, so gating this on one left a `::before` with a background colour invisible.
        bool const is_element_node = node.dom_node && node.dom_node->type == DomNodeType::Element;
        if (auto shadow = node.style.get("box-shadow")) { PaintCommand command; command.type=PaintCommand::Type::DrawShadow; command.rect=node.rect; command.opacity=opacity; std::istringstream stream(*shadow); std::string x,y,blur,color; stream>>x>>y>>blur>>color; command.shadow_offset_x=number(&x,0); command.shadow_offset_y=number(&y,0); command.shadow_blur=number(&blur,0); command.color=color.empty()?"black":color; commands.push_back(std::move(command)); }
        if (node.background_image) { PaintCommand command; command.type=PaintCommand::Type::DrawImage; command.rect=node.box.content; command.image=node.background_image; command.image_fit="fill"; command.background_repeat=node.style.get("background-repeat") ? *node.style.get("background-repeat") : "repeat"; command.background_position=node.style.get("background-position") ? *node.style.get("background-position") : "0 0"; command.background_size=node.style.get("background-size") ? *node.style.get("background-size") : "auto"; command.opacity=opacity; commands.push_back(std::move(command)); }
        if (is_element_node && node.dom_node->name == "img" && node.image) { PaintCommand command; command.type = PaintCommand::Type::DrawImage; command.rect = node.box.content; command.image = node.image; command.image_fit = node.style.get("object-fit") ? *node.style.get("object-fit") : "fill"; command.opacity = opacity; commands.push_back(std::move(command)); }
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
    };
    if ((node.dom_node && node.dom_node->type == DomNodeType::Element) || node.is_generated)
        paint_box();
    if (!node.text_fragments.empty()) {
        for (auto const& fragment : node.text_fragments) {
            PaintCommand command;
            command.type = PaintCommand::Type::DrawText;
            command.rect = fragment.rect;
            command.baseline = fragment.baseline;
            command.text = fragment.text;
            command.opacity = opacity;
            command.font_size = css_length_px(node.style.get("font-size"), 16.0f, 16.0f);
            if (auto const* family = node.style.get("font-family"))
                command.font_family = *family;
            command.letter_spacing = css_length_px(node.style.get("letter-spacing"), command.font_size, 0.0f);
            command.word_spacing = css_length_px(node.style.get("word-spacing"), command.font_size, 0.0f);
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

    // A sticky box is offset at paint time, so it can end up on top of a sibling that follows it in the
    // document; `position: sticky` creates a stacking context, which is what puts it above the in-flow
    // content instead of underneath it. Sticky children therefore sort after the in-flow ones, before the
    // z-index comparison. (Absolute and fixed children keep their document order here - a separate
    // question, and changing it would move pixels this row is not about.)
    auto is_sticky_child = [](LayoutNode const& child) {
        auto const* position = child.style.get("position");
        return position && *position == "sticky";
    };
    bool any_sticky = false;
    for (auto const& child : node.children) {
        if (z_index(*child) != 0)
            any_z_index = true;
        if (is_sticky_child(*child))
            any_sticky = true;
        ordered_children.push_back(child.get());
    }
    // Only pay for a stable sort when a child actually sets a z-index or is sticky; the common case is that
    // every child is auto (0) and the order is unchanged.
    if (any_z_index || any_sticky) {
        std::stable_sort(ordered_children.begin(), ordered_children.end(), [&](auto const* a, auto const* b) {
            if (z_index(*a) != z_index(*b))
                return z_index(*a) < z_index(*b);
            return static_cast<int>(is_sticky_child(*a)) < static_cast<int>(is_sticky_child(*b));
        });
    }
    for (auto const* child : ordered_children)
        emit(*child, commands, opacity, cumulative, sticky_regions, opacity_overrides);

    if (clips_children) {
        std::size_t tail_start = commands.size();
        PaintCommand command;
        command.pop_clip = true;
        commands.push_back(std::move(command));
        // The pop belongs to this node, so it carries this node's transform.
        for (std::size_t index = tail_start; index < commands.size(); ++index)
            apply_transform(cumulative, commands[index]);
    }

    // The sticky shift covers this box and everything inside it: the range is recorded here, after the
    // subtree has been emitted, and paint applies it (row 3b).
    if (is_sticky && commands.size() > own_start) {
        StickyRegion region;
        region.begin = own_start;
        region.end = commands.size();
        region.node_top = node.rect.y;
        region.node_height = node.rect.height;
        auto const& block = node.parent ? node.parent->rect : node.rect;
        region.containing_top = block.y;
        region.containing_bottom = block.y + block.height;
        if (auto const* top = node.style.get("top"); top && !top->empty() && *top != "auto") {
            region.has_top = true;
            region.top = css_length_px(top, node.rect.height, 0.0f);
        }
        if (auto const* bottom = node.style.get("bottom"); bottom && !bottom->empty() && *bottom != "auto") {
            region.has_bottom = true;
            region.bottom = css_length_px(bottom, node.rect.height, 0.0f);
        }
        sticky_regions.push_back(region);
    }
}

} // namespace aetheris::rendering

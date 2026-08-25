#include "layout.hpp"

#include "text_layout.hpp"

#include <algorithm>
#include <cstdlib>

namespace aetheris::rendering {

LayoutNode& LayoutNode::append_child(std::unique_ptr<LayoutNode> child)
{
    child->parent = this;
    children.push_back(std::move(child));
    return *children.back();
}

std::unique_ptr<LayoutNode> LayoutTreeBuilder::build(DomNode const& document, CssStyleSheet const& sheet) const
{
    return build_node(document, sheet, nullptr, nullptr);
}

std::unique_ptr<LayoutNode> LayoutTreeBuilder::build_node(DomNode const& node, CssStyleSheet const& sheet, LayoutNode* parent, StyleProperties const* parent_style) const
{
    auto layout_node = std::make_unique<LayoutNode>();
    layout_node->dom_node = &node;
    layout_node->parent = parent;
    layout_node->style = StyleResolver {}.resolve(node, sheet, parent_style);
    layout_node->display = display_for(node, layout_node->style);

    for (auto const& child : node.children)
        layout_node->append_child(build_node(*child, sheet, layout_node.get(), &layout_node->style));
    return layout_node;
}

LayoutDisplay LayoutTreeBuilder::display_for(DomNode const& node, StyleProperties const& style)
{
    if (auto value = style.get("display")) {
        if (*value == "none")
            return LayoutDisplay::None;
        if (*value == "inline")
            return LayoutDisplay::Inline;
        if (*value == "inline-block")
            return LayoutDisplay::InlineBlock;
        if (*value == "flex" || *value == "inline-flex")
            return LayoutDisplay::Flex;
        if (*value == "grid" || *value == "inline-grid")
            return LayoutDisplay::Grid;
    }
    if (node.type == DomNodeType::Text)
        return LayoutDisplay::Inline;
    return LayoutDisplay::Block;
}

void LayoutEngine::layout(LayoutNode& root, float viewport_width) const
{
    layout_node(root, 0, 0, viewport_width);
}

bool LayoutEngine::is_positioned(LayoutNode const& node)
{
    auto position = node.style.get("position");
    return position && (*position == "absolute" || *position == "fixed");
}

void LayoutEngine::layout_node(LayoutNode& node, float x, float y, float available_width)
{
    if (node.display == LayoutDisplay::None) {
        node.rect = {};
        node.box = {};
        node.text_fragments.clear();
        return;
    }

    node.box.margin = resolve_edges(node.style, "margin", available_width);
    node.box.padding = resolve_edges(node.style, "padding", available_width);
    node.box.border = resolve_edges(node.style, "border", available_width);

    auto specified_width = resolve_dimension(node.style, "width", available_width, -1);
    auto min_width = resolve_dimension(node.style, "min-width", available_width, 0);
    auto max_width = resolve_dimension(node.style, "max-width", available_width, -1);

    float horizontal_noncontent = node.box.margin.left + node.box.margin.right
        + node.box.padding.left + node.box.padding.right
        + node.box.border.left + node.box.border.right;

    float content_width = specified_width >= 0
        ? specified_width
        : std::max(0.0f, available_width - horizontal_noncontent);

    if (max_width >= 0)
        content_width = std::min(content_width, max_width);
    content_width = std::max(content_width, min_width);

    node.box.content.x = x + node.box.margin.left + node.box.border.left + node.box.padding.left;
    node.box.content.y = y + node.box.margin.top + node.box.border.top + node.box.padding.top;
    node.box.content.width = content_width;

    if (node.dom_node && node.dom_node->type == DomNodeType::Element && node.dom_node->name == "img") {
        float intrinsic_width = 0;
        float intrinsic_height = 0;
        if (auto value = node.dom_node->attribute("width")) intrinsic_width = parse_length(value, 0, available_width);
        if (auto value = node.dom_node->attribute("height")) intrinsic_height = parse_length(value, 0, available_width);
        if (specified_width < 0 && intrinsic_width > 0) { content_width = intrinsic_width; node.box.content.width = content_width; }
        float ratio = intrinsic_width > 0 && intrinsic_height > 0 ? intrinsic_height / intrinsic_width : 0.75f;
        float specified_height = parse_length(node.style.get("height"), -1, content_width);
        node.box.content.height = specified_height >= 0 ? specified_height : (intrinsic_height > 0 ? intrinsic_height : content_width * ratio);
    } else if (node.dom_node && node.dom_node->type == DomNodeType::Text) {
        node.text_fragments = TextLayout::layout(node.dom_node->data, node.box.content.x, node.box.content.y, content_width, node.style);
        float line_height = TextLayout::line_height(node.style);
        node.box.content.height = node.text_fragments.empty() ? 0
            : node.text_fragments.back().rect.y + line_height - node.box.content.y;
    } else {
        float cursor_y = node.box.content.y;
        bool has_inline = false;
        for (auto const& child : node.children) {
            if (!is_positioned(*child) && (child->display == LayoutDisplay::Inline || child->display == LayoutDisplay::InlineBlock))
                has_inline = true;
        }

        if (node.display == LayoutDisplay::Flex)
            layout_flex_children(node, content_width, cursor_y);
        else if (node.display == LayoutDisplay::Grid)
            layout_grid_children(node, content_width, cursor_y);
        else if (has_inline)
            layout_inline_children(node, node.box.content.x, cursor_y, content_width, cursor_y);
        else {
            for (auto& child : node.children) {
                if (child->display == LayoutDisplay::None || is_positioned(*child))
                    continue;
                layout_node(*child, node.box.content.x, cursor_y, content_width);
                cursor_y = child->rect.y + child->rect.height;
            }
        }

        float content_height = cursor_y - node.box.content.y;
        float specified_height = parse_length(node.style.get("height"), -1, content_width);
        float min_height = parse_length(node.style.get("min-height"), 0, content_width);
        float max_height = parse_length(node.style.get("max-height"), -1, content_width);

        node.box.content.height = specified_height >= 0 ? specified_height : content_height;
        if (auto ratio = node.style.get("aspect-ratio")) {
            auto slash = ratio->find('/');
            float rw = std::strtof(ratio->c_str(), nullptr);
            float rh = slash == std::string::npos ? 1.0f : std::strtof(ratio->c_str() + slash + 1, nullptr);
            if (rw > 0 && rh > 0 && specified_height < 0) node.box.content.height = content_width * rh / rw;
        }
        node.box.content.height = std::max(node.box.content.height, min_height);
        if (max_height >= 0)
            node.box.content.height = std::min(node.box.content.height, max_height);

        layout_absolute_children(node, content_width);
    }

    node.rect.x = x + node.box.margin.left;
    node.rect.y = y + node.box.margin.top;
    node.rect.width = content_width + node.box.padding.left + node.box.padding.right + node.box.border.left + node.box.border.right;
    node.rect.height = node.box.content.height + node.box.padding.top + node.box.padding.bottom + node.box.border.top + node.box.border.bottom;

    auto position = node.style.get("position");
    if (position && *position == "relative") {
        node.rect.x += parse_length(node.style.get("left"), 0, available_width) - parse_length(node.style.get("right"), 0, available_width);
        node.rect.y += parse_length(node.style.get("top"), 0, available_width) - parse_length(node.style.get("bottom"), 0, available_width);
        node.box.content.x += parse_length(node.style.get("left"), 0, available_width) - parse_length(node.style.get("right"), 0, available_width);
        node.box.content.y += parse_length(node.style.get("top"), 0, available_width) - parse_length(node.style.get("bottom"), 0, available_width);
    }
}

void LayoutEngine::layout_inline_children(LayoutNode& node, float x, float y, float available_width, float& cursor_y)
{
    float cursor_x = x;
    float line_height = TextLayout::line_height(node.style);
    float max_bottom = y;

    for (auto& child : node.children) {
        if (child->display == LayoutDisplay::None || is_positioned(*child))
            continue;

        if (child->display == LayoutDisplay::Inline && child->dom_node && child->dom_node->type == DomNodeType::Text) {
            child->box.content.x = cursor_x;
            child->box.content.y = cursor_y;
            child->box.content.width = std::max(0.0f, x + available_width - cursor_x);
            child->text_fragments = TextLayout::layout(child->dom_node->data, cursor_x, cursor_y, std::max(0.0f, x + available_width - cursor_x), child->style);
            if (!child->text_fragments.empty()) {
                for (auto const& fragment : child->text_fragments) {
                    if (fragment.rect.x < cursor_x && cursor_x > x)
                        cursor_y += line_height;
                    cursor_x = fragment.rect.x + fragment.rect.width;
                    cursor_y = fragment.rect.y;
                    max_bottom = std::max(max_bottom, fragment.rect.y + fragment.rect.height);
                }
                child->box.content.height = max_bottom - child->box.content.y;
                child->rect = { child->box.content.x, child->box.content.y, child->box.content.width, child->box.content.height };
            }
            continue;
        }

        layout_node(*child, cursor_x, cursor_y, x + available_width - cursor_x);
        if (cursor_x > x && child->rect.width > x + available_width - cursor_x) {
            cursor_x = x;
            cursor_y = max_bottom;
            layout_node(*child, cursor_x, cursor_y, available_width);
        }
        cursor_x = child->rect.x + child->rect.width;
        max_bottom = std::max(max_bottom, child->rect.y + child->rect.height);
        if (cursor_x >= x + available_width) {
            cursor_x = x;
            cursor_y = max_bottom;
        }
    }

    cursor_y = std::max(max_bottom, cursor_y + line_height);
}

void LayoutEngine::layout_flex_children(LayoutNode& node, float available_width, float& cursor_y)
{
    std::vector<LayoutNode*> items;
    for (auto& child : node.children) {
        if (child->display != LayoutDisplay::None && !is_positioned(*child))
            items.push_back(child.get());
    }
    if (items.empty())
        return;

    auto direction = node.style.get("flex-direction");
    bool column = direction && (*direction == "column" || *direction == "column-reverse");
    bool reverse = direction && (*direction == "row-reverse" || *direction == "column-reverse");
    if (reverse)
        std::reverse(items.begin(), items.end());

    float gap = parse_length(node.style.get("gap"), 0, available_width);
    if (column) {
        float cursor = node.box.content.y;
        float max_width = 0;
        for (auto* child : items) {
            layout_node(*child, node.box.content.x, cursor, available_width);
            cursor = child->rect.y + child->rect.height + gap;
            max_width = std::max(max_width, child->rect.width);
        }
        cursor_y = items.empty() ? cursor_y : cursor - gap;
        node.box.content.width = std::max(node.box.content.width, max_width);
        return;
    }

    float total_fixed = gap * std::max(0, static_cast<int>(items.size()) - 1);
    float total_grow = 0;
    for (auto* child : items) {
        float grow = parse_length(child->style.get("flex-grow"), 0, available_width);
        total_grow += std::max(0.0f, grow);
        auto width = resolve_dimension(child->style, "width", available_width, -1);
        if (width >= 0) total_fixed += width;
    }
    float free_space = std::max(0.0f, available_width - total_fixed);
    float cursor_x = node.box.content.x;
    float max_height = 0;
    auto justify = node.style.get("justify-content");
    float extra_gap = 0;
    if (justify && *justify == "center") cursor_x += free_space / 2;
    else if (justify && *justify == "flex-end") cursor_x += free_space;
    else if (justify && *justify == "space-between" && items.size() > 1) extra_gap = free_space / (items.size() - 1);
    else if (justify && *justify == "space-around") { extra_gap = free_space / items.size(); cursor_x += extra_gap / 2; }

    for (auto* child : items) {
        float width = resolve_dimension(child->style, "width", available_width, -1);
        float grow = std::max(0.0f, parse_length(child->style.get("flex-grow"), 0, available_width));
        if (width < 0) width = total_grow > 0 ? free_space * grow / total_grow : 0;
        layout_node(*child, cursor_x, node.box.content.y, width);
        auto align = node.style.get("align-items");
        if (align && *align == "center") {
            child->rect.y += std::max(0.0f, max_height - child->rect.height) / 2;
        }
        cursor_x = child->rect.x + child->rect.width + gap + extra_gap;
        max_height = std::max(max_height, child->rect.height);
    }
    if (auto align = node.style.get("align-items"); align && *align == "flex-end") {
        for (auto* child : items) child->rect.y += max_height - child->rect.height;
    }
    cursor_y = node.box.content.y + max_height;
}

void LayoutEngine::layout_grid_children(LayoutNode& node, float available_width, float& cursor_y)
{
    std::vector<LayoutNode*> items;
    for (auto& child : node.children)
        if (child->display != LayoutDisplay::None && !is_positioned(*child)) items.push_back(child.get());
    if (items.empty()) return;

    auto parse_tracks = [&](std::string const* value) {
        std::vector<float> tracks;
        if (!value) return tracks;
        std::string token; float fixed = 0; int fr_count = 0;
        for (size_t i = 0; i <= value->size(); ++i) {
            char c = i < value->size() ? (*value)[i] : ' ';
            if (c == ' ' || c == '\t' || i == value->size()) {
                if (!token.empty()) {
                    if (token.size() >= 2 && token.substr(token.size()-2) == "fr") { tracks.push_back(-std::max(0.0f, std::strtof(token.c_str(), nullptr))); ++fr_count; }
                    else { float v = parse_length(&token, 0, available_width); tracks.push_back(v); fixed += v; }
                    token.clear();
                }
            } else token += c;
        }
        float gap = parse_length(node.style.get("column-gap"), parse_length(node.style.get("gap"), 0, available_width), available_width);
        float remaining = std::max(0.0f, available_width - fixed - gap * std::max(0, (int)tracks.size()-1));
        float fr_total = 0; for (float v : tracks) if (v < 0) fr_total += -v;
        for (float& v : tracks) if (v < 0) v = fr_total > 0 ? remaining * (-v) / fr_total : 0;
        return tracks;
    };

    auto columns = parse_tracks(node.style.get("grid-template-columns"));
    if (columns.empty()) columns.push_back(available_width);
    float gap_x = parse_length(node.style.get("column-gap"), parse_length(node.style.get("gap"), 0, available_width), available_width);
    float gap_y = parse_length(node.style.get("row-gap"), parse_length(node.style.get("gap"), 0, available_width), available_width);
    size_t cols = columns.size(); std::vector<float> row_heights;
    for (size_t index = 0; index < items.size(); ++index) {
        size_t col = index % cols, row = index / cols;
        while (row_heights.size() <= row) row_heights.push_back(0);
        float x = node.box.content.x; for (size_t c=0;c<col;++c) x += columns[c] + gap_x;
        float y = node.box.content.y; for (size_t r=0;r<row;++r) y += row_heights[r] + gap_y;
        layout_node(*items[index], x, y, columns[col]);
        row_heights[row] = std::max(row_heights[row], items[index]->rect.height);
    }
    float total = 0; for (float h : row_heights) total += h; total += gap_y * std::max(0, (int)row_heights.size()-1);
    cursor_y = node.box.content.y + total;
}

void LayoutEngine::layout_absolute_children(LayoutNode& node, float available_width)
{
    for (auto& child : node.children) {
        auto position = child->style.get("position");
        if (!position || (*position != "absolute" && *position != "fixed"))
            continue;

        float child_width = resolve_dimension(child->style, "width", available_width, 0);
        float left = parse_length(child->style.get("left"), -1, available_width);
        float right = parse_length(child->style.get("right"), -1, available_width);
        if (child_width <= 0 && left >= 0 && right >= 0)
            child_width = std::max(0.0f, available_width - left - right);

        float base_x = *position == "fixed" ? 0 : node.box.content.x;
        float base_y = *position == "fixed" ? 0 : node.box.content.y;
        float child_x = left >= 0 ? base_x + left : (right >= 0 ? base_x + available_width - right - child_width : base_x);
        float child_y = base_y + parse_length(child->style.get("top"), 0, node.box.content.height);

        layout_node(*child, child_x, child_y, child_width > 0 ? child_width : available_width);
    }
}

float LayoutEngine::parse_length(std::string const* value, float fallback, float percentage_base)
{
    if (!value || value->empty() || *value == "auto")
        return fallback;
    char* end = nullptr;
    auto parsed = std::strtof(value->c_str(), &end);
    if (end == value->c_str())
        return fallback;
    if (end && *end == '%')
        return percentage_base * parsed / 100.0f;
    return parsed;
}

float LayoutEngine::resolve_dimension(StyleProperties const& style, char const* property, float available_width, float fallback)
{
    return parse_length(style.get(property), fallback, available_width);
}

BoxEdges LayoutEngine::resolve_edges(StyleProperties const& style, char const* prefix, float percentage_base)
{
    std::string base(prefix);
    auto side = [&](char const* name) {
        auto direct = base + "-" + name;
        if (auto value = style.get(direct))
            return parse_length(value, 0, percentage_base);
        if (base == "border") {
            auto width = std::string("border-") + name + "-width";
            if (auto value = style.get(width))
                return parse_length(value, 0, percentage_base);
        }
        return 0.0f;
    };
    return { side("top"), side("right"), side("bottom"), side("left") };
}

} // namespace aetheris::rendering

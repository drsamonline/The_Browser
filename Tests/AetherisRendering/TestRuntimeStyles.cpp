#include "render_document.hpp"
#include "visual_state.hpp"
#include <cassert>
#include <functional>
using namespace aetheris::rendering;
int main() {
    auto page = RenderDocument::create("<html><body><button id='go'>Go</button></body></html>", "button { color: black; } button:hover { color: red; } button:focus { background-color: blue; }", 320);
    DomNode const* button = nullptr;
    std::function<void(DomNode const&)> walk = [&](DomNode const& node) {
        if (node.type == DomNodeType::Element && node.name == "button") button = &node;
        for (auto const& child : node.children) walk(*child);
    };
    walk(page.document().root());
    assert(button);
    VisualInteractionState state;
    state.set_hovered(button);
    page.apply_visual_state(state, 320);
    std::function<LayoutNode const*(LayoutNode const&)> find_layout = [&](LayoutNode const& node) -> LayoutNode const* {
        if (node.dom_node == button) return &node;
        for (auto const& child : node.children) if (auto* found = find_layout(*child)) return found;
        return nullptr;
    };
    auto* layout_button = find_layout(page.layout_root());
    assert(layout_button && layout_button->style.get("color") && *layout_button->style.get("color") == "red");
    state.set_focused(button);
    page.apply_visual_state(state, 320);
    assert(page.visual_generation() == state.generation);
    return 0;
}

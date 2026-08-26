#pragma once
#include "dom.hpp"
#include <cstddef>
namespace aetheris::rendering {
struct VisualInteractionState {
    DomNode const* hovered { nullptr };
    DomNode const* active { nullptr };
    DomNode const* focused { nullptr };
    std::size_t generation { 0 };
    bool layout_dirty { false };
    bool paint_dirty { false };
    bool set_hovered(DomNode const*);
    bool set_active(DomNode const*);
    bool set_focused(DomNode const*);
    void mark_layout_dirty();
    void mark_paint_dirty();
    bool consume_layout_dirty();
    bool consume_paint_dirty();
};
}

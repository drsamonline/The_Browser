#pragma once
#include "dom.hpp"
#include <cstddef>
namespace aetheris::rendering {
// Which device moved the focus. Upstream keeps this as HTML::FocusTrigger and
// `should_indicate_focus()` uses it to decide whether :focus-visible matches.
enum class FocusTrigger {
    Pointer,
    Key,
};

struct VisualInteractionState {
    DomNode const* hovered { nullptr };
    DomNode const* active { nullptr };
    DomNode const* focused { nullptr };
    FocusTrigger focus_trigger { FocusTrigger::Pointer };
    std::size_t generation { 0 };
    bool layout_dirty { false };
    bool paint_dirty { false };
    bool set_hovered(DomNode const*);
    bool set_active(DomNode const*);
    bool set_focused(DomNode const*);
    void mark_layout_dirty();
    bool consume_layout_dirty();
    bool consume_paint_dirty();
};
}

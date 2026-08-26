#include "visual_state.hpp"
namespace aetheris::rendering {
static bool set_node(DomNode const*& slot, DomNode const* value, VisualInteractionState& state) { if (slot == value) return false; slot=value; ++state.generation; state.paint_dirty=true; return true; }
bool VisualInteractionState::set_hovered(DomNode const* n){return set_node(hovered,n,*this);} 
bool VisualInteractionState::set_active(DomNode const* n){return set_node(active,n,*this);} 
bool VisualInteractionState::set_focused(DomNode const* n){return set_node(focused,n,*this);} 
void VisualInteractionState::mark_layout_dirty(){layout_dirty=true; paint_dirty=true; ++generation;} void VisualInteractionState::mark_paint_dirty(){paint_dirty=true; ++generation;}
bool VisualInteractionState::consume_layout_dirty(){bool v=layout_dirty; layout_dirty=false; return v;} bool VisualInteractionState::consume_paint_dirty(){bool v=paint_dirty; paint_dirty=false; return v;}
}

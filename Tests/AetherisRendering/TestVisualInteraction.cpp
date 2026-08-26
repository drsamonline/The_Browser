#include "rendering/visual_state.hpp"
#include <cassert>
using namespace aetheris::rendering;
int main(){ VisualInteractionState s; assert(s.set_hovered(nullptr)==false); DomNode n; assert(s.set_hovered(&n)); assert(s.paint_dirty); auto g=s.generation; assert(!s.set_hovered(&n)); s.set_active(&n); s.set_focused(&n); s.mark_layout_dirty(); assert(s.consume_layout_dirty()); assert(!s.consume_layout_dirty()); assert(s.consume_paint_dirty()); assert(s.generation>g); }

#include "render_document.hpp"
#include "render_tree.hpp"
#include <cassert>
using namespace aetheris::rendering; int main(){auto d=RenderDocument::create("<div>hello</div>","div{text-decoration:underline;color:red}");auto tree=RenderTree::from_layout(d.layout_tree());bool found=false;for(auto const& c:tree.commands()) if(c.type==PaintCommand::Type::DrawTextDecoration) found=true;assert(found);}

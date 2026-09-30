#include "render_document.hpp"
#include "render_tree.hpp"
#include <cassert>
using namespace aetheris::rendering; int main(){auto d=RenderDocument::create("<div>hello</div>","div{text-decoration:underline;color:red}",800.0f);bool found=false;for(auto const& c:d.render_tree().commands()) if(c.type==PaintCommand::Type::DrawTextDecoration) found=true;assert(found);}

#include "css_parser.hpp"
#include "dom.hpp"
#include "layout.hpp"
#include <cassert>
using namespace aetheris::rendering;
int main(){
 auto root=std::make_unique<DomNode>(); root->type=DomNodeType::Document;
 auto grid=std::make_unique<DomNode>(); grid->type=DomNodeType::Element; grid->name="div";
 for(int i=0;i<4;i++){auto c=std::make_unique<DomNode>();c->type=DomNodeType::Element;c->name="span";grid->append_child(std::move(c));}
 root->append_child(std::move(grid)); CssParser p; auto s=p.parse("div{display:grid;width:100;grid-template-columns:1fr 1fr;gap:10;}span{height:10;}"); auto t=LayoutTreeBuilder{}.build(*root,s); LayoutEngine{}.layout(*t,100); auto& g=*t->children[0];
 assert(g.children[1]->rect.x > g.children[0]->rect.x); assert(g.children[2]->rect.y > g.children[0]->rect.y); assert(g.children[0]->rect.width > 40);
}

#include "css_parser.hpp"
#include "dom.hpp"
#include "layout.hpp"
#include <cassert>
using namespace aetheris::rendering;
int main(){ auto r=std::make_unique<DomNode>();r->type=DomNodeType::Document;auto b=std::make_unique<DomNode>();b->type=DomNodeType::Element;b->name="div";for(int i=0;i<2;i++){auto c=std::make_unique<DomNode>();c->type=DomNodeType::Element;c->name="span";b->append_child(std::move(c));}r->append_child(std::move(b));CssParser p;auto s=p.parse("div{display:flex;width:100;justify-content:space-between;}span{width:20;height:10;}");auto t=LayoutTreeBuilder{}.build(*r,s);LayoutEngine{}.layout(*t,100);auto& x=*t->children[0];assert(x.children[1]->rect.x>x.children[0]->rect.x+20); }

#include "css_parser.hpp"
#include "dom.hpp"
#include "layout.hpp"
#include <cassert>
using namespace aetheris::rendering;
int main(){ auto root=std::make_unique<DomNode>();root->type=DomNodeType::Document;auto box=std::make_unique<DomNode>();box->type=DomNodeType::Element;box->name="div";root->append_child(std::move(box));CssParser p;auto s=p.parse("div{width:80;aspect-ratio:2/1;}");auto t=LayoutTreeBuilder{}.build(*root,s);LayoutEngine{}.layout(*t,100);auto& b=*t->children[0];assert(b.box.content.height>39 && b.box.content.height<41); }

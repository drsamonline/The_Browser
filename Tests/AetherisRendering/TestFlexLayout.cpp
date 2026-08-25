#include "css_parser.hpp"
#include "dom.hpp"
#include "layout.hpp"
#include <cassert>
using namespace aetheris::rendering;
static std::unique_ptr<LayoutNode> build(char const* css) {
    auto root = std::make_unique<DomNode>(); root->type=DomNodeType::Document;
    auto box = std::make_unique<DomNode>(); box->type=DomNodeType::Element; box->name="div";
    for (int i=0;i<2;++i) { auto c=std::make_unique<DomNode>(); c->type=DomNodeType::Element; c->name="span"; box->append_child(std::move(c)); }
    root->append_child(std::move(box));
    CssParser parser; auto sheet=parser.parse(css); LayoutTreeBuilder builder; auto tree=builder.build(*root,sheet); LayoutEngine{}.layout(*tree,100); return tree;
}
int main(){
 auto t=build("div { display:flex; gap:10; width:100; } span { width:20; height:10; }"); auto& b=*t->children[0]; assert(b.children[1]->rect.x >= b.children[0]->rect.x+30-0.01f);
 auto c=build("div { display:flex; flex-direction:column; gap:5; width:100; } span { height:10; }"); auto& cb=*c->children[0]; assert(cb.children[1]->rect.y >= cb.children[0]->rect.y+15-0.01f);
 auto g=build("div { display:flex; width:100; } span { flex-grow:1; height:10; }"); auto& gb=*g->children[0]; assert(gb.children[0]->rect.width > 40 && gb.children[1]->rect.width > 40);
}

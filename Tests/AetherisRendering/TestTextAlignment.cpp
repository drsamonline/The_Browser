#include "text_layout.hpp"
#include <cassert>
using namespace aetheris::rendering; int main(){StyleProperties s;s.set("text-align","center");auto a=TextLayout::layout("hello",0,0,100,s);assert(!a.empty()&&a[0].rect.x>0);s.set("text-align","right");auto b=TextLayout::layout("hello",0,0,100,s);assert(b[0].rect.x>a[0].rect.x);}

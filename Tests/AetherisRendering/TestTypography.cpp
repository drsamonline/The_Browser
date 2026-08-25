#include "text_layout.hpp"
#include <cassert>
using namespace aetheris::rendering; int main(){StyleProperties s;s.set("font-size","20");s.set("line-height","2");assert(TextLayout::line_height(s)==40);s.set("font-style","italic");assert(TextLayout::font_size(s)==20);}

#include <font.hpp>
#include <text_layout.hpp>
#include <cstdlib>
#include <iostream>
using namespace aetheris::rendering; static void req(bool v,char const*m){if(!v){std::cerr<<"FAILED: "<<m<<'\n';std::exit(1);}}
int main(){StyleProperties s;s.set("font-size","20px");s.set("letter-spacing","1px");auto f=Font::from_style(s);req(f.metrics().size==20,"font size");req(f.measure("Hello")>50,"font measure");auto a=TextLayout::layout("alpha beta gamma",0,0,60,s);req(a.size()>=3,"font aware wrapping");return 0;}

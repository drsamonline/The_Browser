#include <render_document.hpp>
#include <cstdlib>
#include <iostream>
using namespace aetheris::rendering;
static void req(bool v,char const*m){if(!v){std::cerr<<"FAILED: "<<m<<'\n';std::exit(1);}}
int main(){ResourceCache r;r.put({ResourceType::Image,"memory://test.ppm",{'P','3','\n','2',' ','1','\n','2','5','5','\n','2','5','5',' ','0',' ','0',' ','0',' ','2','5','5',' ','0','\n'}});auto d=RenderDocument::create("<img src='memory://test.ppm' width='20' height='10'>","",40,&r);req(d.layout_root().children[0]->image!=nullptr,"image resolved");auto s=d.render_to_surface(40,20);auto p=s.pixel(1,1);req(p.red>200,"image painted");req(s.pixel(15,1).green>200,"image scaled");return 0;}

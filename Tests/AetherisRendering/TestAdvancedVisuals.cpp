#include "render_document.hpp"
#include <cassert>
#include <cstring>
using namespace aetheris::rendering;
int main(){
 ResourceCache resources; ResourceData d; d.type=ResourceType::Image; d.url="bg.ppm"; const char* ppm="P3 2 1 255 255 0 0 0 0 255"; d.bytes.assign(ppm,ppm+strlen(ppm)); resources.put(std::move(d));
 auto doc=RenderDocument::create("<div><img src='bg.ppm'></div>", "div{width:20px;height:20px;background-image:url(bg.ppm);box-shadow:2px 2px 2px black;outline:1px solid red} img{width:10px;height:10px;object-fit:contain}", 100, &resources);
 auto s=doc.render_to_surface(40,40); assert(s.width()==40); return 0;
}

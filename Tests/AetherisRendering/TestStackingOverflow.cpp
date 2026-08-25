#include "render_document.hpp"
#include <cassert>
using namespace aetheris::rendering;
int main() {
    auto document = RenderDocument::create("<div class='a'></div><div class='b'></div>",
        ".a{width:20px;height:20px;background-color:red;position:absolute;left:0;top:0;z-index:1}.b{width:20px;height:20px;background-color:blue;position:absolute;left:0;top:0;z-index:2}", 40);
    auto surface=document.render_to_surface(40,40);
    assert(surface.pixel(5,5).blue==255);
    auto clipped=RenderDocument::create("<div><div></div></div>", ".a{overflow:hidden;width:10px;height:10px}.a div{width:30px;height:30px;background-color:red}", 40);
    (void)clipped;
    return 0;
}

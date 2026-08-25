#include "software_surface.hpp"
#include <cassert>
using namespace aetheris::rendering;
int main() {
    Image image; image.width=1; image.height=1; image.pixels={Color{255,0,0,255}};
    SoftwareSurface surface(8,8);
    surface.draw_background_image({0,0,8,8}, image, "repeat", "0 0", "auto");
    assert(surface.pixel(7,7).red==255);
    return 0;
}

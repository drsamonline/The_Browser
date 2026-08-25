#include "image_output.hpp"

#include <fstream>

namespace aetheris::rendering {

bool ImageOutput::write_ppm(SoftwareSurface const& surface, std::string const& path)
{
    std::ofstream output(path, std::ios::binary);
    if (!output)
        return false;

    output << "P6\n" << surface.width() << ' ' << surface.height() << "\n255\n";
    for (int y = 0; y < surface.height(); ++y) {
        for (int x = 0; x < surface.width(); ++x) {
            auto pixel = surface.pixel(x, y);
            output.put(static_cast<char>(pixel.red));
            output.put(static_cast<char>(pixel.green));
            output.put(static_cast<char>(pixel.blue));
        }
    }
    return output.good();
}

} // namespace aetheris::rendering

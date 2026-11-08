#include "font_library.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace aetheris::rendering {

namespace {

std::vector<uint8_t> read_file(std::string const& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

TtfFont* load_font()
{
    std::vector<std::string> candidates;
    if (auto* from_env = std::getenv("AETHERIS_FONT")) {
        // "none" forces the embedded bitmap font, which is what the test suite
        // and a font-free machine use.
        if (std::string(from_env) == "none")
            return nullptr;
        candidates.push_back(from_env);
    }
    candidates.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    candidates.push_back("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
    candidates.push_back("/usr/local/lib/python3.12/site-packages/matplotlib/mpl-data/fonts/ttf/DejaVuSans.ttf");

    for (auto const& path : candidates) {
        auto data = read_file(path);
        if (data.empty())
            continue;
        auto* font = new TtfFont;
        if (font->load(data))
            return font;
        delete font;
    }
    return nullptr;
}

} // namespace

TtfFont const* active_font()
{
    static TtfFont* font = load_font();
    return font;
}

} // namespace aetheris::rendering

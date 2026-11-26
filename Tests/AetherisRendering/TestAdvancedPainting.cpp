#include <image_output.hpp>
#include <render_document.hpp>
#include <software_surface.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace aetheris::rendering;

static void require(bool condition, char const* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    auto rendered = RenderDocument::create(
        "<div><span>clip me</span></div>",
        "div { width: 30px; height: 20px; background-color: red; border-width: 2px; border-color: blue;"
        "border-radius: 8px; border-style: dashed; overflow: hidden; opacity: 0.5; }"
        "span { color: white; font-size: 18px; white-space: nowrap; }",
        80);

    bool rounded = false;
    bool stroked = false;
    for (auto const& command : rendered.render_tree().commands()) {
        rounded = rounded || command.type == PaintCommand::Type::FillRoundedRect;
        stroked = stroked || command.type == PaintCommand::Type::StrokeRoundedRect;
    }
    require(rounded, "rounded background command");
    require(stroked, "rounded border command");

    auto surface = rendered.render_to_surface(80, 40);
    auto corner = surface.pixel(0, 0);

    // The rounded corner lies outside the div, so the background shows through
    // there while the div body is painted. Text is now real glyphs (not a solid
    // cell), so scan for the composited white-text pixels rather than sampling a
    // single fixed point.
    bool body_painted = false;
    bool text_composited = false;
    for (int y = 0; y < surface.height(); ++y) {
        for (int x = 0; x < surface.width(); ++x) {
            auto pixel = surface.pixel(x, y);
            if (pixel.red > corner.red)
                body_painted = true;
            if (pixel.red > 100 && pixel.green > 0 && pixel.blue > 0)
                text_composited = true;
        }
    }
    require(body_painted, "rounded corner leaves background visible");
    require(text_composited, "opacity compositing applied");

    // Row 0.14: `box-shadow` is a list of shadows, `none` is the empty list, the colour is optional and
    // may appear anywhere in it, and an outer shadow is not painted inside its own border box. Read
    // positionally as "x y blur colour", `box-shadow: none` painted an opaque black rectangle the size of
    // the box - a 976x78 black band across a real page's title bar - and a light `0 1px #c8ccd1` the same
    // way, with the colour landing in the blur slot.
    {
        auto const shadows_of = [](char const* css) {
            auto rendered = RenderDocument::create(
                "<div id='box' style='width: 60px; height: 30px'>x</div>", css, 200);
            std::vector<PaintCommand> found;
            for (auto const& command : rendered.render_tree().commands())
                if (command.type == PaintCommand::Type::DrawShadow)
                    found.push_back(command);
            return found;
        };
        require(shadows_of("#box { box-shadow: none }").empty(),
            "box-shadow: none paints no shadow at all");
        auto const light = shadows_of("#box { box-shadow: 0 1px #c8ccd1 }");
        require(light.size() == 1, "a single shadow is one command");
        require(light[0].color == "#c8ccd1",
            "the colour is read as the colour, not as the blur radius");
        require(light[0].shadow_offset_y == 1.0f && light[0].shadow_blur == 0.0f,
            "and the two lengths are the offsets");
        auto const inherited = shadows_of("#box { color: #ff0000; box-shadow: 0 2px }");
        require(inherited.size() == 1 && inherited[0].color == "#ff0000",
            "a shadow with no colour takes the element's own colour, as currentColor says");
        require(shadows_of("#box { box-shadow: 4px }").empty(),
            "one length is not a shadow");

        // An outer shadow is clipped to outside the border box: whatever the box itself would cover is
        // not painted. Called on a surface directly, because the document path paints no page background
        // and would answer "nothing was drawn" for both the right and the wrong behaviour.
        {
            SoftwareSurface surface(120, 80, Color { 255, 255, 255, 255 });
            surface.draw_shadow(LayoutRect { 10, 10, 40, 20 }, 0, 10, 0, Color { 255, 0, 0, 255 });
            auto const inside = surface.pixel(30, 20);
            auto const below = surface.pixel(30, 35);
            require(inside.red > 200 && inside.green > 200 && inside.blue > 200,
                "the shadow is not painted inside the border box");
            require(below.red > 200 && below.green < 100 && below.blue < 100,
                "and it is painted outside it, in the band its offset moves it into");
        }
    }

    auto path = std::filesystem::temp_directory_path() / "aetheris_batch09.ppm";
    require(ImageOutput::write_ppm(surface, path.string()), "image output");
    std::filesystem::remove(path);
    return 0;
}

// Scrolling is three separable things: the viewport clamps a scroll position,
// the painter offsets every command by it, and the scrollbar maps between the
// two. Each is checked here without a window, which is the point of keeping the
// geometry out of the SDL backend.

#include <render_document.hpp>
#include <scroll_bar.hpp>
#include <software_surface.hpp>
#include <viewport.hpp>

#include <cstdlib>
#include <iostream>

using namespace aetheris::rendering;

static void require(bool condition, char const* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

static bool approximately(float a, float b, float tolerance = 0.5f)
{
    return (a > b ? a - b : b - a) <= tolerance;
}

static void check_viewport_clamping()
{
    Viewport viewport(800, 600);
    viewport.set_document_size(800, 2000);

    viewport.scroll_to(0, 500);
    require(approximately(viewport.scroll_y(), 500), "scroll_to sets the offset");

    // Past the end clamps to document height minus viewport height.
    viewport.scroll_to(0, 5000);
    require(approximately(viewport.scroll_y(), 1400), "scroll_to clamps at the document end");

    viewport.scroll_to(0, -400);
    require(approximately(viewport.scroll_y(), 0), "scroll_to clamps at the document start");

    viewport.scroll_to(0, 1000);
    viewport.scroll_by(0, 500);
    require(approximately(viewport.scroll_y(), 1400), "scroll_by clamps at the document end");

    // A document shorter than the viewport cannot scroll at all.
    Viewport short_viewport(800, 600);
    short_viewport.set_document_size(800, 300);
    short_viewport.scroll_to(0, 200);
    require(approximately(short_viewport.scroll_y(), 0), "a short document does not scroll");

    // Horizontal scrolling works on the same clamp.
    Viewport wide(800, 600);
    wide.set_document_size(2400, 600);
    wide.scroll_to(1000, 0);
    require(approximately(wide.scroll_x(), 1000), "horizontal scroll is independent");
}

static void check_paint_offset()
{
    auto document = RenderDocument::create(
        "<div id='a'></div><div id='b'></div>",
        "#a { height: 1000px; background-color: #ff0000; }"
        "#b { height: 1000px; background-color: #0000ff; }",
        200.0f);

    require(document.document_rect().height > 1500.0f, "tall document is laid out");

    auto at_top = document.render_to_surface(200, 400, Color { 255, 255, 255, 255 }, PaintOffset { 0, 0 });
    auto top_left = at_top.pixel(10, 10);
    require(top_left.red == 255 && top_left.blue == 0, "top of the document paints the first block");

    // Scrolling by 1000 px must put the second block at the top of the surface.
    auto scrolled = document.render_to_surface(200, 400, Color { 255, 255, 255, 255 }, PaintOffset { 0, 1000 });
    auto scrolled_left = scrolled.pixel(10, 10);
    require(scrolled_left.blue == 255 && scrolled_left.red == 0, "paint offset moves content up");

    // And the first block must be gone entirely, not merely off-screen: commands
    // outside the surface are culled rather than rasterised and discarded.
    bool any_red = false;
    for (int y = 0; y < scrolled.height(); ++y)
        for (int x = 0; x < scrolled.width(); ++x) {
            auto pixel = scrolled.pixel(x, y);
            any_red = any_red || (pixel.red > 200 && pixel.blue < 50);
        }
    require(!any_red, "content above the viewport is not painted");
}

static void check_scrollbar_geometry()
{
    // A document that fits has no scrollbar.
    auto fitting = ScrollBar::geometry(1024, 600, 600, 0);
    require(!fitting.visible, "no scrollbar when the document fits");
    require(approximately(fitting.max_scroll, 0), "no scroll range when the document fits");

    auto bar = ScrollBar::geometry(1024, 600, 2000, 0);
    require(bar.visible, "scrollbar appears on a long document");
    require(approximately(bar.max_scroll, 1400), "max scroll is document minus viewport");
    require(approximately(bar.track.x, 1024 - ScrollBar::thickness()), "track is right-aligned");
    require(approximately(bar.track.height, 600), "track spans the viewport");
    require(approximately(bar.thumb.height, 180), "thumb is proportional to the visible fraction");
    require(approximately(bar.thumb.y, 0), "thumb starts at the top");

    auto at_end = ScrollBar::geometry(1024, 600, 2000, 1400);
    require(approximately(at_end.thumb.y, 420), "thumb reaches the bottom at max scroll");

    // A very long document keeps the thumb grabbable.
    auto huge = ScrollBar::geometry(1024, 600, 100000, 0);
    require(approximately(huge.thumb.height, ScrollBar::minimum_thumb_height()), "thumb has a floor");

    // Thumb position tracks scroll monotonically.
    float previous = -1;
    for (int i = 0; i <= 10; ++i) {
        auto step = ScrollBar::geometry(1024, 600, 2000, static_cast<float>(i) * 140.0f);
        require(step.thumb.y >= previous, "thumb moves down as scroll increases");
        previous = step.thumb.y;
    }
}

static void check_scrollbar_pointer()
{
    auto bar = ScrollBar::geometry(1024, 600, 2000, 0);
    require(approximately(ScrollBar::scroll_for_pointer(bar, 0), 0), "click at the top scrolls to the top");
    require(approximately(ScrollBar::scroll_for_pointer(bar, 600), bar.max_scroll), "click at the bottom scrolls to the end");
    require(approximately(ScrollBar::scroll_for_pointer(bar, -50), 0), "click above the track clamps");
    require(approximately(ScrollBar::scroll_for_pointer(bar, 5000), bar.max_scroll), "click below the track clamps");

    auto middle = ScrollBar::scroll_for_pointer(bar, 300);
    require(middle > 0 && middle < bar.max_scroll, "click in the middle lands between the ends");

    // A hidden bar maps every pointer position to the top.
    auto hidden = ScrollBar::geometry(1024, 600, 400, 0);
    require(approximately(ScrollBar::scroll_for_pointer(hidden, 300), 0), "hidden bar does not scroll");
}

static void check_scrollbar_paint()
{
    SoftwareSurface surface(200, 100, Color { 255, 255, 255, 255 });
    auto bar = ScrollBar::geometry(200, 100, 400, 0);
    require(bar.visible, "bar visible for the paint check");

    ScrollBar::paint(surface, bar, Color { 200, 200, 200, 255 }, Color { 0, 0, 0, 255 });

    // The thumb is at the top; the track below it is the track colour.
    auto thumb = surface.pixel(196, 2);
    require(thumb.red < 40 && thumb.green < 40 && thumb.blue < 40, "thumb is painted dark");
    auto track = surface.pixel(196, 99);
    require(track.red > 150 && track.red < 230, "track is painted light");
    auto page = surface.pixel(10, 10);
    require(page.red == 255 && page.green == 255, "page area is untouched");
}

int main()
{
    check_viewport_clamping();
    check_paint_offset();
    check_scrollbar_geometry();
    check_scrollbar_pointer();
    check_scrollbar_paint();
    std::cout << "scrolling: all checks passed" << std::endl;
    return 0;
}

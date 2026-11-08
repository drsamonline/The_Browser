// The window host is the one part of the UI that cannot be exercised on a
// machine without a display, so its contract is tested through the headless
// backend: open, present, poll and close must behave predictably.

#include <software_surface.hpp>
#include <ui/window_host.hpp>

#include <cstdio>
#include <cstdlib>

using namespace aetheris;

static void require(bool condition, char const* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

int main()
{
    rendering::SoftwareSurface surface(64, 48, rendering::Color { 10, 20, 30, 255 });

    // A headless host accepts exactly its frame budget and then reports closed.
    {
        ui::HeadlessWindowHost host(3);
        require(host.open(1024, 768, "Aetheris"), "headless host opens");
        require(host.is_open(), "host is open after open()");

        for (int frame = 0; frame < 3; ++frame) {
            require(host.is_open(), "host stays open until the budget is spent");
            host.present(surface);
        }
        require(host.frames_presented() == 3, "every presented frame is counted");
        require(!host.is_open(), "host closes once the frame budget is spent");

        // Presenting after close must not resurrect it or count a frame.
        host.present(surface);
        require(host.frames_presented() == 3, "presenting a closed host does nothing");
        require(!host.is_open(), "a closed host stays closed");
    }

    // poll() must never fill an event in a headless host: returning true with an
    // empty event would spin a caller's loop forever.
    {
        ui::HeadlessWindowHost host(1);
        host.open(100, 100, "t");
        ui::WindowEvent event;
        require(!host.poll(event), "headless poll reports no events");
        require(event.kind == ui::WindowEvent::Kind::None, "no event is synthesised");
    }

    // The title is stored so a host can display it.
    {
        ui::HeadlessWindowHost host(1);
        host.open(100, 100, "first");
        require(host.title() == "first", "title is stored on open");
        host.set_title("second");
        require(host.title() == "second", "title can be changed");
    }

    // The factory must always produce a usable host, with or without a display.
    {
        auto host = ui::make_window_host(1);
        require(host != nullptr, "the factory returns a host");
        require(host->open(320, 240, "factory"), "the factory host opens");
        host->present(surface);
        require(!host->is_open(), "the factory host honours its frame budget");
    }

    std::printf("window host: %s backend\n", ui::has_display_backend() ? "display" : "headless");
    return 0;
}

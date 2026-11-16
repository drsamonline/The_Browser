// Input routing decides what a keystroke or click means. It is a pure function
// of (event, focus state), so every binding is checked here without a display.

#include <ui/input_router.hpp>

#include <cstdio>
#include <cstdlib>

using namespace aetheris::ui;

static void require(bool condition, char const* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

static WindowEvent key(std::string name, bool control = false, bool alt = false)
{
    WindowEvent event;
    event.kind = WindowEvent::Kind::KeyDown;
    event.key = std::move(name);
    event.control = control;
    event.alt = alt;
    return event;
}

static WindowEvent click(int x, int y)
{
    WindowEvent event;
    event.kind = WindowEvent::Kind::MouseDown;
    event.x = x;
    event.y = y;
    return event;
}

int main()
{
    // Ctrl+shortcuts work regardless of focus.
    {
        InputRouter router;
        require(router.handle(key("l", true)).kind == InputAction::Kind::FocusAddress, "Ctrl+L focuses the address bar");
        require(router.address_focused(), "address bar is focused after Ctrl+L");
        require(router.handle(key("t", true)).kind == InputAction::Kind::NewTab, "Ctrl+T opens a tab");
        require(router.handle(key("w", true)).kind == InputAction::Kind::CloseTab, "Ctrl+W closes a tab");
        require(router.handle(key("r", true)).kind == InputAction::Kind::Reload, "Ctrl+R reloads");
        require(router.handle(key("Left", false, true)).kind == InputAction::Kind::Back, "Alt+Left goes back");
        require(router.handle(key("Right", false, true)).kind == InputAction::Kind::Forward, "Alt+Right goes forward");
        require(router.handle(key("F11")).kind == InputAction::Kind::ToggleZen, "F11 toggles zen mode");
        require(router.zen_mode(), "zen mode is on after F11");
    }

    // Typing goes to the address bar only when it has focus.
    {
        InputRouter router;
        WindowEvent typed;
        typed.kind = WindowEvent::Kind::TextInput;
        typed.text = "x";
        require(router.handle(typed).kind == InputAction::Kind::None, "typing is ignored when the page has focus");

        router.handle(key("l", true));
        router.set_address_text("example.com");
        auto edited = router.handle(typed);
        require(edited.kind == InputAction::Kind::AddressEdited, "typing edits the address");
        require(edited.text == "example.comx", "the typed character is appended");

        auto backspaced = router.handle(key("BackSpace"));
        require(backspaced.text == "example.com", "backspace removes a character");

        auto navigated = router.handle(key("Enter"));
        require(navigated.kind == InputAction::Kind::Navigate, "Enter navigates");
        require(navigated.text == "example.com", "the address is the target");
        require(!router.address_focused(), "focus leaves the address bar after navigating");
    }

    // Escape blurs the address bar rather than quitting, and quits from the page.
    {
        InputRouter router;
        router.handle(key("l", true));
        require(router.handle(key("Escape")).kind == InputAction::Kind::BlurAddress, "Escape blurs the address bar");
        require(!router.address_focused(), "focus returns to the page");
        require(router.handle(key("Escape")).kind == InputAction::Kind::Quit, "Escape on the page quits");
    }

    // Clicks: chrome regions vs page, split at the strip height.
    {
        InputRouter router;
        auto page_click = router.handle(click(500, 400));
        require(page_click.kind == InputAction::Kind::PageClick, "a click below the strip goes to the page");
        require(page_click.x == 500 && page_click.y == 400, "page clicks keep their coordinates");

        require(router.handle(click(InputRouter::address_x() + 10, 15)).kind == InputAction::Kind::FocusAddress,
            "clicking the address bar focuses it");
        require(router.address_focused(), "the click focused the address bar");

        InputRouter buttons;
        require(buttons.handle(click(InputRouter::back_x(), 15)).kind == InputAction::Kind::Back, "back button");
        require(buttons.handle(click(InputRouter::forward_x(), 15)).kind == InputAction::Kind::Forward, "forward button");
        require(buttons.handle(click(InputRouter::reload_x(), 15)).kind == InputAction::Kind::Reload, "reload button");

        // The very first page pixel is page, not chrome.
        require(buttons.handle(click(10, 34)).kind == InputAction::Kind::PageClick, "y=34 is page content");
        require(buttons.handle(click(10, 33)).kind != InputAction::Kind::PageClick, "y=33 is still chrome");
    }

    // Hover only reaches the page.
    {
        InputRouter router;
        WindowEvent hover;
        hover.kind = WindowEvent::Kind::MouseMove;
        hover.x = 100;
        hover.y = 200;
        require(router.handle(hover).kind == InputAction::Kind::PageHover, "hover reaches the page");
        hover.y = 10;
        require(router.handle(hover).kind == InputAction::Kind::None, "hover over the chrome is ignored");
    }

    // Closing the window quits.
    {
        InputRouter router;
        WindowEvent closed;
        closed.kind = WindowEvent::Kind::Close;
        require(router.handle(closed).kind == InputAction::Kind::Quit, "closing the window quits");
    }

    // The wheel scrolls the page, and its sign is preserved for the caller.
    {
        InputRouter router;
        WindowEvent wheel;
        wheel.kind = WindowEvent::Kind::Scroll;
        wheel.delta = 3;
        auto down = router.handle(wheel);
        require(down.kind == InputAction::Kind::Scroll, "wheel produces a scroll action");
        require(down.delta == 3, "wheel delta is passed through");

        wheel.delta = -2;
        require(router.handle(wheel).delta == -2, "negative wheel delta is preserved");

        // A wheel event carries no pointer position, so it must not be dropped
        // just because y defaults to the chrome strip.
        wheel.delta = 1;
        wheel.y = 0;
        require(router.handle(wheel).kind == InputAction::Kind::Scroll, "wheel over the chrome still scrolls");

        // A zero delta is not an action worth dispatching.
        wheel.delta = 0;
        require(router.handle(wheel).kind == InputAction::Kind::None, "zero wheel delta is ignored");
    }

    // Keyboard scrolling belongs to the page, not the address bar.
    {
        InputRouter router;
        require(router.handle(key("PageDown")).kind == InputAction::Kind::ScrollPage, "PageDown pages down");
        require(router.handle(key("PageDown")).delta == 1, "PageDown is positive");
        require(router.handle(key("PageUp")).delta == -1, "PageUp is negative");
        require(router.handle(key("Home")).kind == InputAction::Kind::ScrollEdge, "Home jumps to the top");
        require(router.handle(key("Home")).delta == -1, "Home is negative");
        require(router.handle(key("End")).delta == 1, "End is positive");

        // With the address bar focused these keys must not scroll the page out
        // from under the user.
        InputRouter focused;
        focused.handle(key("l", true));
        require(focused.address_focused(), "address bar took focus");
        require(focused.handle(key("PageDown")).kind != InputAction::Kind::ScrollPage, "no paging while typing");
        require(focused.handle(key("Home")).kind != InputAction::Kind::ScrollEdge, "no Home jump while typing");
    }

    // A click on the scrollbar belongs to the bar, not to the page under it.
    {
        InputRouter router;
        router.set_scroll_bar(true, 1024, 10);

        auto on_bar = router.handle(click(1020, 200));
        require(on_bar.kind == InputAction::Kind::ScrollTrack, "right edge is the scrollbar");
        require(on_bar.y == 200, "bar click carries its y for the geometry");

        require(router.handle(click(1013, 200)).kind == InputAction::Kind::PageClick, "left of the bar is the page");
        require(router.handle(click(1020, 10)).kind != InputAction::Kind::ScrollTrack, "bar does not extend into the chrome");

        // With no bar drawn, the same click is an ordinary page click.
        router.set_scroll_bar(false, 1024, 10);
        require(router.handle(click(1020, 200)).kind == InputAction::Kind::PageClick, "no bar, no bar clicks");
    }

    std::printf("input router: all bindings verified\n");
    return 0;
}

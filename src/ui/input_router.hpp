#pragma once

#include "window_host.hpp"

#include <string>

namespace aetheris::ui {

// What the browser should do in response to input. The router decides; the
// caller acts. Keeping it a pure decision makes every binding testable without
// a display.
struct InputAction {
    enum class Kind {
        None,
        Navigate,      // `text` holds the URL to load
        Reload,
        Back,
        Forward,
        NewTab,
        CloseTab,
        FocusAddress,  // the address bar takes keyboard focus
        BlurAddress,   // the page takes keyboard focus again
        AddressEdited, // the address text changed; `text` holds the new value
        PageClick,     // x, y in window coordinates
        PageHover,     // x, y in window coordinates
        Scroll,        // `delta` wheel notches: positive scrolls down
        ScrollPage,    // `delta` pages: positive scrolls down, negative up
        ScrollEdge,    // `delta` -1 to the top of the document, +1 to the bottom
        ScrollTrack,   // click on the scrollbar; x, y in window coordinates
        ToggleZen,     // hide or show the chrome
        Quit,
    };

    Kind kind { Kind::None };
    std::string text;
    int x { 0 };
    int y { 0 };
    int delta { 0 };

    InputAction() = default;
    // Explicit constructor rather than aggregate initialisation: leaving fields
    // out of a braced initialiser warns under -Wmissing-field-initializers, and
    // CI builds with -Werror.
    explicit InputAction(Kind action_kind, std::string action_text = {}, int action_x = 0, int action_y = 0, int action_delta = 0)
        : kind(action_kind)
        , text(std::move(action_text))
        , x(action_x)
        , y(action_y)
        , delta(action_delta)
    {
    }
};

// Routes window events to either the chrome or the page. The chrome owns the
// top `chrome_height` pixels; everything below belongs to the page.
class InputRouter {
public:
    explicit InputRouter(int chrome_height = 34);

    // Feed one event; returns what the browser should do.
    InputAction handle(WindowEvent const& event);

    bool address_focused() const { return m_address_focused; }
    std::string const& address_text() const { return m_address; }
    void set_address_text(std::string text) { m_address = std::move(text); }
    bool zen_mode() const { return m_zen; }

    // Regions of the chrome strip, in window coordinates. Exposed so the same
    // numbers drive both drawing and hit-testing.
    static int tab_width() { return 190; }
    static int back_x() { return 230; }
    static int forward_x() { return 256; }
    static int reload_x() { return 282; }
    static int address_x() { return 310; }
    static int address_width() { return 660; }

    // Tells the router whether a scrollbar is currently drawn along the right
    // edge of the page area. The router does not own the scroll geometry — it
    // only needs to know whether a click at the edge belongs to the bar or to
    // the page, so a click can never fall through to a link underneath.
    void set_scroll_bar(bool visible, int content_width, int bar_width = 10)
    {
        m_scroll_bar_visible = visible;
        m_content_width = content_width;
        m_scroll_bar_width = bar_width;
    }
    bool scroll_bar_visible() const { return m_scroll_bar_visible; }

    // Pixels per wheel notch. A notch is three lines, and a line is the 20 px
    // body leading, so one notch is 60 px — the same feel as a desktop browser.
    static int scroll_pixels_per_notch() { return 60; }

private:
    InputAction handle_key(WindowEvent const& event);
    InputAction handle_click(WindowEvent const& event);

    int m_chrome_height { 34 };
    bool m_scroll_bar_visible { false };
    int m_content_width { 0 };
    int m_scroll_bar_width { 10 };
    bool m_address_focused { false };
    bool m_zen { false };
    std::string m_address;
};

} // namespace aetheris::ui

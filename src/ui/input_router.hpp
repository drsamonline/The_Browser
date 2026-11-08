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
        ToggleZen,     // hide or show the chrome
        Quit,
    };

    Kind kind { Kind::None };
    std::string text;
    int x { 0 };
    int y { 0 };

    InputAction() = default;
    // Explicit constructor rather than aggregate initialisation: leaving fields
    // out of a braced initialiser warns under -Wmissing-field-initializers, and
    // CI builds with -Werror.
    explicit InputAction(Kind action_kind, std::string action_text = {}, int action_x = 0, int action_y = 0)
        : kind(action_kind)
        , text(std::move(action_text))
        , x(action_x)
        , y(action_y)
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

private:
    InputAction handle_key(WindowEvent const& event);
    InputAction handle_click(WindowEvent const& event);

    int m_chrome_height { 34 };
    bool m_address_focused { false };
    bool m_zen { false };
    std::string m_address;
};

} // namespace aetheris::ui

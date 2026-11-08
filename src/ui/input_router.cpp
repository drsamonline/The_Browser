#include "input_router.hpp"

#include <cctype>

namespace aetheris::ui {

InputRouter::InputRouter(int chrome_height)
    : m_chrome_height(chrome_height > 0 ? chrome_height : 34)
{
}

InputAction InputRouter::handle(WindowEvent const& event)
{
    switch (event.kind) {
    case WindowEvent::Kind::Close:
        return InputAction { InputAction::Kind::Quit };
    case WindowEvent::Kind::KeyDown:
        return handle_key(event);
    case WindowEvent::Kind::TextInput:
        if (!m_address_focused)
            return InputAction {};
        m_address += event.text;
        return InputAction { InputAction::Kind::AddressEdited, m_address };
    case WindowEvent::Kind::MouseDown:
        return handle_click(event);
    case WindowEvent::Kind::MouseMove:
        // Only the page tracks hover; the chrome has no hover styling yet.
        if (event.y >= m_chrome_height)
            return InputAction { InputAction::Kind::PageHover, {}, event.x, event.y };
        return InputAction {};
    default:
        return InputAction {};
    }
}

InputAction InputRouter::handle_key(WindowEvent const& event)
{
    std::string key = event.key;
    for (auto& c : key)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // Shortcuts work wherever focus is, matching browser convention.
    if (event.control) {
        if (key == "l") {
            m_address_focused = true;
            return InputAction { InputAction::Kind::FocusAddress, m_address };
        }
        if (key == "t")
            return InputAction { InputAction::Kind::NewTab };
        if (key == "w")
            return InputAction { InputAction::Kind::CloseTab };
        if (key == "r")
            return InputAction { InputAction::Kind::Reload };
    }
    if (event.alt) {
        if (key == "left")
            return InputAction { InputAction::Kind::Back };
        if (key == "right")
            return InputAction { InputAction::Kind::Forward };
    }
    if (key == "f11") {
        m_zen = !m_zen;
        return InputAction { InputAction::Kind::ToggleZen };
    }

    if (m_address_focused) {
        if (key == "escape") {
            m_address_focused = false;
            return InputAction { InputAction::Kind::BlurAddress, m_address };
        }
        if (key == "enter") {
            m_address_focused = false;
            auto action = InputAction { InputAction::Kind::Navigate, m_address };
            return action;
        }
        if (key == "backspace") {
            if (!m_address.empty()) {
                m_address.pop_back();
                return InputAction { InputAction::Kind::AddressEdited, m_address };
            }
            return InputAction {};
        }
        // Other keys are typing, delivered as TextInput by the host.
        return InputAction {};
    }

    if (key == "escape")
        return InputAction { InputAction::Kind::Quit };
    return InputAction {};
}

InputAction InputRouter::handle_click(WindowEvent const& event)
{
    // Above the strip is chrome; below it is page.
    if (event.y >= m_chrome_height)
        return InputAction { InputAction::Kind::PageClick, {}, event.x, event.y };

    if (event.x < tab_width())
        return InputAction {}; // activating a tab needs the tab list; not yet
    if (event.x >= address_x() && event.x < address_x() + address_width()) {
        m_address_focused = true;
        return InputAction { InputAction::Kind::FocusAddress, m_address };
    }
    // Navigation buttons are drawn at these positions; a click near one fires it.
    if (event.x >= back_x() - 12 && event.x < back_x() + 12)
        return InputAction { InputAction::Kind::Back };
    if (event.x >= forward_x() - 12 && event.x < forward_x() + 12)
        return InputAction { InputAction::Kind::Forward };
    if (event.x >= reload_x() - 12 && event.x < reload_x() + 12)
        return InputAction { InputAction::Kind::Reload };
    return InputAction {};
}

} // namespace aetheris::ui

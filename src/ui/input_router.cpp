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
    case WindowEvent::Kind::MouseUp:
        // The press ended: whatever was :active stops being :active.
        if (event.y >= m_chrome_height)
            return InputAction { InputAction::Kind::Release, {}, event.x, event.y };
        return InputAction {};
    case WindowEvent::Kind::Scroll:
        // A wheel event carries no pointer position on every backend, so it is
        // never gated on the chrome height: a wheel over the chrome scrolls the
        // page, which is what every browser does.
        if (event.delta == 0)
            return InputAction {};
        return InputAction { InputAction::Kind::Scroll, {}, event.x, event.y, event.delta };
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
        if (key == "h")
            return InputAction { InputAction::Kind::ShowHistory };
        if (key == "b")
            return InputAction { InputAction::Kind::ShowBookmarks };
        if (key == "d")
            return InputAction { InputAction::Kind::ToggleBookmark };
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

    // Page-level scrolling keys. They belong to the page, so they only fire
    // when the address bar does not hold focus.
    if (!m_address_focused) {
        if (key == "space" || key == "pagedown")
            return InputAction { InputAction::Kind::ScrollPage, {}, 0, 0, 1 };
        if (key == "pageup")
            return InputAction { InputAction::Kind::ScrollPage, {}, 0, 0, -1 };
        if (key == "home")
            return InputAction { InputAction::Kind::ScrollEdge, {}, 0, 0, -1 };
        if (key == "end")
            return InputAction { InputAction::Kind::ScrollEdge, {}, 0, 0, 1 };
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

    if (key == "tab")
        return InputAction { InputAction::Kind::FocusNext };


    // Only now does a key belong to the page. Placing this before the browser's
    // own claims swallowed Escape and the page never quit - the fallback has to
    // come last, or it steals every key the browser was going to handle.
    if (!key.empty())
        return InputAction { InputAction::Kind::Key, key };
    return InputAction {};
}

InputAction InputRouter::handle_click(WindowEvent const& event)
{
    // Above the strip is chrome; below it is page.
    if (event.y >= m_chrome_height) {
        // The scrollbar owns the right edge of the page area while it is drawn.
        // Checking it first is what stops a click on the bar from also being a
        // click on whatever link happens to be underneath it.
        if (m_scroll_bar_visible && m_content_width > 0
            && event.x >= m_content_width - m_scroll_bar_width)
            return InputAction { InputAction::Kind::ScrollTrack, {}, event.x, event.y };
        return InputAction { InputAction::Kind::PageClick, {}, event.x, event.y };
    }

    // Everything in the strip is decided by the layout that drew it: one source of truth for the tabs a page paints
    // and the tabs a click can reach (row 4.1).
    auto const hit = m_layout.hit_test(event.x, event.y);
    switch (hit.kind) {
    case ChromeLayout::Hit::Tab:
        return InputAction { InputAction::Kind::ActivateTab, {}, event.x, event.y, 0, hit.index };
    case ChromeLayout::Hit::CloseTab:
        return InputAction { InputAction::Kind::CloseTab, {}, event.x, event.y, 0, hit.index };
    case ChromeLayout::Hit::NewTab:
        return InputAction { InputAction::Kind::NewTab };
    case ChromeLayout::Hit::Back:
        return InputAction { InputAction::Kind::Back };
    case ChromeLayout::Hit::Forward:
        return InputAction { InputAction::Kind::Forward };
    case ChromeLayout::Hit::Reload:
        return InputAction { InputAction::Kind::Reload };
    case ChromeLayout::Hit::Address:
        m_address_focused = true;
        return InputAction { InputAction::Kind::FocusAddress, m_address };
    case ChromeLayout::Hit::None:
        break;
    }
    return InputAction {};
}

} // namespace aetheris::ui

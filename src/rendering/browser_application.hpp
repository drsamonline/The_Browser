#pragma once
#include "browser_session.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace aetheris::rendering {
class BrowserApplication {
public:
    struct Tab { BrowserSession session; };
    enum class PointerButton { Primary, Secondary, Middle };
    explicit BrowserApplication(float viewport_width = 1024, float viewport_height = 768);
    std::size_t new_tab(); bool close_tab(std::size_t); bool switch_to_tab(std::size_t);
    std::size_t tab_count() const { return m_tabs.size(); } std::optional<std::size_t> active_tab_index() const { return m_active; }
    BrowserSession* active_session(); BrowserSession const* active_session() const;
    NavigationResult navigate(NavigationRequest const&, ResourceLoader&); NavigationResult reload(ResourceLoader&); NavigationResult back(ResourceLoader&); NavigationResult forward(ResourceLoader&);
    bool pointer_move(float,float); NavigationResult pointer_press(float,float,PointerButton,ResourceLoader&); bool pointer_release(float,float,PointerButton);
    bool text_input(std::string const&); bool backspace(); void resize(float,float); void scroll(float,float);
private:
    std::vector<Tab> m_tabs; std::optional<std::size_t> m_active; float m_width; float m_height; bool m_primary_pressed { false };
};
}

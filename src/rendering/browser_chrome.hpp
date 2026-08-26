#pragma once
#include "browser_application.hpp"
#include <string>
#include <vector>
namespace aetheris::rendering {
class BrowserChrome {
public:
    enum class PageState { Idle, Loading, Ready, Error };
    explicit BrowserChrome(BrowserApplication& application);
    bool set_address(std::string address);
    std::string const& address() const { return m_address; }
    std::string const& status_text() const { return m_status; }
    PageState page_state() const { return m_state; }
    bool can_go_back() const; bool can_go_forward() const;
    std::size_t tab_count() const; std::size_t new_tab(); bool close_active_tab(); bool activate_tab(std::size_t);
    NavigationResult navigate(ResourceLoader&); NavigationResult reload(ResourceLoader&); NavigationResult back(ResourceLoader&); NavigationResult forward(ResourceLoader&);
private:
    void sync_status(NavigationResult const&);
    BrowserApplication& m_application; std::string m_address; std::string m_status { "Ready" }; PageState m_state { PageState::Idle };
};
}

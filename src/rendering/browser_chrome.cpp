#include "browser_chrome.hpp"

namespace aetheris::rendering {

BrowserChrome::BrowserChrome(BrowserApplication& app)
    : m_application(app)
{
}

bool BrowserChrome::set_address(std::string address)
{
    if (address.empty())
        return false;
    m_address = std::move(address);
    return true;
}

bool BrowserChrome::can_go_back() const
{
    auto* session = m_application.active_session();
    return session && session->can_go_back();
}

bool BrowserChrome::can_go_forward() const
{
    auto* session = m_application.active_session();
    return session && session->can_go_forward();
}

std::size_t BrowserChrome::tab_count() const
{
    return m_application.tab_count();
}

std::size_t BrowserChrome::new_tab()
{
    return m_application.new_tab();
}

bool BrowserChrome::close_active_tab()
{
    auto index = m_application.active_tab_index();
    return index && m_application.close_tab(*index);
}

bool BrowserChrome::activate_tab(std::size_t index)
{
    return m_application.switch_to_tab(index);
}

NavigationResult BrowserChrome::navigate(ResourceLoader& loader)
{
    m_state = PageState::Loading;

    NavigationRequest request {
        .document_url = Url::parse(m_address),
        .stylesheet_url = std::nullopt,
        .viewport_width = 1024.0f,
    };

    if (auto* session = m_application.active_session()) {
        if (session->viewport().width() > 0)
            request.viewport_width = session->viewport().width();
    }

    auto result = m_application.navigate(request, loader);
    sync_status(result);
    return result;
}

NavigationResult BrowserChrome::reload(ResourceLoader& loader)
{
    m_state = PageState::Loading;
    auto result = m_application.reload(loader);
    sync_status(result);
    return result;
}

NavigationResult BrowserChrome::back(ResourceLoader& loader)
{
    m_state = PageState::Loading;
    auto result = m_application.back(loader);
    sync_status(result);
    return result;
}

NavigationResult BrowserChrome::forward(ResourceLoader& loader)
{
    m_state = PageState::Loading;
    auto result = m_application.forward(loader);
    sync_status(result);
    return result;
}

void BrowserChrome::sync_status(NavigationResult const& result)
{
    if (result.error == NavigationError::None) {
        m_state = PageState::Ready;
        m_status = "Ready";
    } else {
        m_state = PageState::Error;
        m_status = result.message.empty() ? "Navigation failed" : result.message;
    }
}

} // namespace aetheris::rendering

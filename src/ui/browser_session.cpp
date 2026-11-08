#include "browser_session.hpp"

#include <algorithm>
#include <cctype>

namespace aetheris::ui {

namespace {

std::string lowered(std::string value)
{
    for (auto& c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool has_scheme(std::string const& url)
{
    auto colon = url.find("://");
    if (colon == std::string::npos || colon == 0)
        return false;
    for (std::size_t index = 0; index < colon; ++index) {
        auto c = static_cast<unsigned char>(url[index]);
        if (!std::isalnum(c) && c != '+' && c != '-' && c != '.')
            return false;
    }
    return true;
}

} // namespace

bool BrowserSession::is_loadable(std::string const& url)
{
    if (url.empty())
        return false;
    // A bare host like "example.com" is loadable; it just needs a scheme.
    auto trimmed = url;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())))
        trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
        trimmed.pop_back();
    return !trimmed.empty();
}

std::string BrowserSession::normalise(std::string url)
{
    while (!url.empty() && std::isspace(static_cast<unsigned char>(url.front())))
        url.erase(url.begin());
    while (!url.empty() && std::isspace(static_cast<unsigned char>(url.back())))
        url.pop_back();
    if (url.empty() || has_scheme(url))
        return url;
    // "localhost:8080/x" and "example.com" both want a scheme.
    auto scheme = lowered(url).rfind("localhost", 0) == 0 ? "http://" : "https://";
    return scheme + url;
}

bool BrowserSession::open(std::string url)
{
    if (!is_loadable(url))
        return false;
    m_tabs.clear();
    m_tabs.push_back(BrowserTab { normalise(std::move(url)), {}, true, false, {} });
    m_active = 0;
    return true;
}

bool BrowserSession::navigate(std::string url)
{
    if (!is_loadable(url))
        return false;
    auto* tab = active();
    if (!tab)
        return false;
    tab->url = normalise(std::move(url));
    tab->loading = true;
    tab->failed = false;
    tab->error.clear();
    return true;
}

void BrowserSession::set_page_title(std::string title)
{
    if (auto* tab = active())
        tab->title = std::move(title);
}

void BrowserSession::set_loading(bool loading)
{
    if (auto* tab = active())
        tab->loading = loading;
}

void BrowserSession::set_error(std::string message)
{
    if (auto* tab = active()) {
        tab->loading = false;
        tab->failed = true;
        tab->error = std::move(message);
    }
}

std::string const& BrowserSession::current_url() const
{
    static const std::string empty;
    auto* tab = const_cast<BrowserSession*>(this)->active();
    return tab ? tab->url : empty;
}

std::string const& BrowserSession::current_title() const
{
    static const std::string empty;
    auto* tab = const_cast<BrowserSession*>(this)->active();
    return tab ? tab->title : empty;
}

std::string BrowserSession::display_title() const
{
    auto const& title = current_title();
    if (!title.empty())
        return title;
    auto const& url = current_url();
    if (!url.empty())
        return url;
    return "New Tab";
}

bool BrowserSession::loading() const
{
    auto* tab = const_cast<BrowserSession*>(this)->active();
    return tab && tab->loading;
}

bool BrowserSession::failed() const
{
    auto* tab = const_cast<BrowserSession*>(this)->active();
    return tab && tab->failed;
}

std::string const& BrowserSession::error() const
{
    static const std::string empty;
    auto* tab = const_cast<BrowserSession*>(this)->active();
    return tab ? tab->error : empty;
}

BrowserTab const* BrowserSession::tab(std::size_t index) const
{
    if (index >= m_tabs.size())
        return nullptr;
    return &m_tabs[index];
}

std::size_t BrowserSession::new_tab()
{
    m_tabs.push_back(BrowserTab {});
    m_active = m_tabs.size() - 1;
    return m_active;
}

bool BrowserSession::close_active_tab()
{
    if (m_tabs.empty())
        return false;
    m_tabs.erase(m_tabs.begin() + static_cast<std::ptrdiff_t>(m_active));
    if (m_tabs.empty()) {
        m_active = 0;
        return true;
    }
    if (m_active >= m_tabs.size())
        m_active = m_tabs.size() - 1;
    return true;
}

bool BrowserSession::activate_tab(std::size_t index)
{
    if (index >= m_tabs.size())
        return false;
    m_active = index;
    return true;
}

BrowserTab* BrowserSession::active()
{
    if (m_tabs.empty())
        return nullptr;
    if (m_active >= m_tabs.size())
        m_active = m_tabs.size() - 1;
    return &m_tabs[m_active];
}

} // namespace aetheris::ui

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
    auto normalised = normalise(std::move(url));
    HistoryEntry first;
    first.url = normalised;
    m_tabs.push_back(BrowserTab { normalised, {}, true, false, {}, { std::move(first) }, 0 });
    m_active = 0;
    return true;
}

bool BrowserSession::navigate_in_place(std::string url)
{
    auto* tab = active();
    if (!tab)
        return false;
    tab->url = url;
    // A fragment navigation adds a history entry, and truncates what was ahead.
    tab->entries.resize(tab->index + 1);
    HistoryEntry entry;
    entry.url = tab->url;
    tab->entries.push_back(std::move(entry));
    tab->index = tab->entries.size() - 1;
    tab->loading = false;
    tab->failed = false;
    tab->error.clear();
    return true;
}

bool BrowserSession::navigate(std::string url, bool replace)
{
    if (!is_loadable(url))
        return false;
    auto* tab = active();
    if (!tab)
        return false;
    tab->url = normalise(std::move(url));

    // History: a navigation after going back discards what was ahead of it, and a replacement
    // overwrites the entry it stands on.
    if (tab->entries.empty()) {
        HistoryEntry entry;
        entry.url = tab->url;
        tab->entries.push_back(std::move(entry));
    } else if (replace) {
        // A replacement keeps the entry's scroll and its restoration mode: the entry is the same entry, pointed
        // somewhere else, which is what `location.replace()` means.
        tab->entries[tab->index].url = tab->url;
    } else {
        tab->entries.resize(tab->index + 1);
        HistoryEntry entry;
        entry.url = tab->url;
        tab->entries.push_back(std::move(entry));
        tab->index = tab->entries.size() - 1;
    }
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

std::size_t BrowserSession::history_length() const
{
    auto const* tab = m_tabs.empty() ? nullptr : &m_tabs[m_active];
    return tab ? tab->entries.size() : 0;
}

bool BrowserSession::history_go(int delta)
{
    auto* tab = active();
    if (!tab || tab->entries.empty())
        return false;

    // A traversal that lands outside the session history does nothing, which is what the spec's
    // traversal does when there is no entry to move to.
    long const target = static_cast<long>(tab->index) + delta;
    if (target < 0 || static_cast<std::size_t>(target) >= tab->entries.size())
        return false;

    tab->index = static_cast<std::size_t>(target);
    tab->url = tab->entries[tab->index].url;
    // A traversal is where `scrollRestoration` earns its name: the entry says whether the scroll comes back.
    m_pending_scroll_restore = tab->entries[tab->index].scroll_restoration == ScrollRestoration::Auto;
    m_pending_scroll_x = tab->entries[tab->index].scroll_x;
    m_pending_scroll_y = tab->entries[tab->index].scroll_y;
    tab->loading = true;
    tab->failed = false;
    tab->error.clear();
    return true;
}

ScrollRestoration BrowserSession::scroll_restoration() const
{
    auto const* tab = m_tabs.empty() ? nullptr : &m_tabs[m_active];
    if (!tab || tab->entries.empty())
        return ScrollRestoration::Auto;
    return tab->entries[tab->index].scroll_restoration;
}

void BrowserSession::set_scroll_restoration(ScrollRestoration mode)
{
    auto* tab = active();
    if (!tab || tab->entries.empty())
        return;
    tab->entries[tab->index].scroll_restoration = mode;
}

void BrowserSession::record_scroll(float x, float y)
{
    auto* tab = active();
    if (!tab || tab->entries.empty())
        return;
    tab->entries[tab->index].scroll_x = x;
    tab->entries[tab->index].scroll_y = y;
}

bool BrowserSession::take_scroll_restore(float& x, float& y)
{
    if (!m_pending_scroll_restore)
        return false;
    m_pending_scroll_restore = false;
    x = m_pending_scroll_x;
    y = m_pending_scroll_y;
    return true;
}

namespace {

// Scheme and authority, lowercased. The session is deliberately free of the rendering layer, so it does not reach
// for SecurityPolicy::is_same_origin(); this is the same question asked in eight lines, and it is the only thing
// this layer needs from it.
std::string origin_of(std::string const& url)
{
    auto const scheme_end = url.find("://");
    if (scheme_end == std::string::npos)
        return {};
    auto const path_start = url.find('/', scheme_end + 3);
    auto origin = url.substr(0, path_start == std::string::npos ? std::string::npos : path_start);
    for (auto& c : origin)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return origin;
}

// A `pushState` URL is resolved against the document, as the spec's "parse url, relative to the relevant settings
// object" does: an absolute URL stands alone, "//host/p" takes the current scheme, "/path" the current origin, and
// anything else the current directory.
std::string resolve_against(std::string const& base, std::string const& target)
{
    if (target.find("://") != std::string::npos)
        return target;
    auto const scheme_end = base.find("://");
    if (scheme_end == std::string::npos)
        return target;
    if (target.rfind("//", 0) == 0)
        return base.substr(0, scheme_end) + ":" + target;
    auto const origin = origin_of(base);
    if (target.empty())
        return base;
    if (target.front() == '/')
        return origin + target;
    auto const path_start = base.find('/', scheme_end + 3);
    if (path_start == std::string::npos)
        return origin + "/" + target;
    auto const last_slash = base.rfind('/');
    return base.substr(0, last_slash + 1) + target;
}

} // namespace

bool BrowserSession::push_state(std::string const& url, std::string serialized_state, bool replace)
{
    auto* tab = active();
    if (!tab || tab->entries.empty())
        return false;

    auto const& current_url = tab->entries[tab->index].url;
    auto target = url.empty() ? current_url : resolve_against(current_url, url);
    if (origin_of(target) != origin_of(current_url))
        return false;

    tab->url = target;
    if (replace) {
        tab->entries[tab->index].url = target;
        tab->entries[tab->index].state = std::move(serialized_state);
        tab->entries[tab->index].has_state = true;
        return true;
    }

    tab->entries.resize(tab->index + 1);
    HistoryEntry entry;
    entry.url = target;
    entry.state = std::move(serialized_state);
    entry.has_state = true;
    tab->entries.push_back(std::move(entry));
    tab->index = tab->entries.size() - 1;
    return true;
}

std::string const& BrowserSession::current_state() const
{
    static const std::string empty;
    auto const* tab = m_tabs.empty() ? nullptr : &m_tabs[m_active];
    if (!tab || tab->entries.empty())
        return empty;
    auto const& entry = tab->entries[tab->index];
    return entry.has_state ? entry.state : empty;
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

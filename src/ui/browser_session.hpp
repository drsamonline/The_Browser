#pragma once

#include <string>
#include <vector>

namespace aetheris::ui {

// What the browser is showing: the tabs, which one is active, and the state the
// chrome needs to draw itself. Deliberately free of rendering and networking, so
// the behaviour of tabs and navigation can be tested on its own.
struct BrowserTab {
    std::string url;
    std::string title;
    bool loading { false };
    bool failed { false };
    std::string error;
};

class BrowserSession {
public:
    // Opens a first tab. Returns false when the URL is empty or has no scheme.
    bool open(std::string url);

    // Points the active tab at a new URL. The caller loads it and reports back
    // with set_page_title() / set_loading() / set_error().
    bool navigate(std::string url);

    void set_page_title(std::string title);
    void set_loading(bool loading);
    void set_error(std::string message);

    std::string const& current_url() const;
    std::string const& current_title() const;
    // Title for the tab strip: the page title, else the URL, else "New Tab".
    std::string display_title() const;
    bool loading() const;
    bool failed() const;
    std::string const& error() const;

    std::size_t tab_count() const { return m_tabs.size(); }
    std::size_t active_index() const { return m_active; }
    BrowserTab const* tab(std::size_t index) const;

    // Tabs. new_tab() makes the new tab active and returns its index.
    std::size_t new_tab();
    bool close_active_tab();
    bool activate_tab(std::size_t index);

    // True when a URL looks loadable: non-empty and with a scheme, or a bare
    // host that can be given https://.
    static bool is_loadable(std::string const& url);
    static std::string normalise(std::string url);

private:
    BrowserTab* active();

    std::vector<BrowserTab> m_tabs;
    std::size_t m_active { 0 };
};

} // namespace aetheris::ui

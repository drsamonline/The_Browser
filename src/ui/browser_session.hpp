#pragma once

#include <string>
#include <vector>

namespace aetheris::ui {

enum class ScrollRestoration {
    Auto,
    Manual,
};

struct HistoryEntry {
    std::string url;
    float scroll_x { 0 };
    float scroll_y { 0 };
    ScrollRestoration scroll_restoration { ScrollRestoration::Auto };
    // `history.state`, as the text the JS layer serialised. Empty means "no state", which is what `history.state`
    // reports as null. Upstream stores a structured clone (HTML/History.cpp:177, StructuredSerializeForStorage);
    // this engine has no structured clone, so the binding writes JSON and says so (row 1g4b).
    std::string state;
    bool has_state { false };
    // The page title as it was when the entry was current. A history list needs it: the tab's title only describes
    // the entry the tab is standing on (row 4.2).
    std::string title;
};

// What the browser is showing: the tabs, which one is active, and the state the
// chrome needs to draw itself. Deliberately free of rendering and networking, so
// the behaviour of tabs and navigation can be tested on its own.
struct BrowserTab {
    std::string url;
    std::string title;
    bool loading { false };
    bool failed { false };
    std::string error;

    // Session history. Upstream keeps a session history on the navigable and `History` reads it
    // (HTML/History.idl: length, go, back, forward). One list and an index, truncated on a new
    // navigation and overwritten in place by `location.replace()`.
    //
    // Each entry carries what a traversal has to restore. `scrollRestoration` is per entry, not per document:
    // upstream stores the mode on the session history entry and `History.scrollRestoration` reads and writes the
    // current one (HTML/History.cpp:229-242). `Auto` means a traversal puts the scroll back where it was; `Manual`
    // means the page does it itself (row 1g4c).
    std::vector<HistoryEntry> entries;
    std::size_t index { 0 };
};

class BrowserSession {
public:
    // Opens a first tab. Returns false when the URL is empty or has no scheme.
    bool open(std::string url);

    // Points the active tab at a new URL. The caller loads it and reports back
    // with set_page_title() / set_loading() / set_error().
    //
    // `replace` overwrites the current history entry instead of adding one, which is what
    // `location.replace()` means: the page moves and Back does not return to it.
    bool navigate(std::string url, bool replace = false);

    // A same-document navigation: the URL and the history move, nothing is loaded. Upstream treats a
    // fragment change on the same document this way (DOM/Document.cpp, scroll_to_the_fragment).
    bool navigate_in_place(std::string url);

    // Session history, as `History` exposes it. `go` is the primitive: back() is go(-1) and forward()
    // is go(+1), which is how upstream's History is built (HTML/History.cpp, delta_traverse), and
    // go(0) reloads the current document.
    std::size_t history_length() const;

    // `history.scrollRestoration`: the mode of the entry the tab is standing on, as upstream keeps it.
    ScrollRestoration scroll_restoration() const;
    void set_scroll_restoration(ScrollRestoration mode);

    // What the page was scrolled to when it was left. A tab calls this before navigating or traversing.
    void record_scroll(float x, float y);

    // After a traversal, the offset the new entry wants restored, if its mode is Auto and it has one. Answers
    // once: a second call says no, so a repaint cannot re-apply an old offset.
    bool take_scroll_restore(float& x, float& y);

    // The active tab's history, oldest first, for a history panel. Read-only: the panel links to URLs and a click
    // goes through the ordinary navigation path.
    std::vector<HistoryEntry> const& history_entries() const;

    // Bookmarks: a flat list, which is what this browser has room for. `add` keeps the first title it is given for
    // a URL, so a bookmark made before the page loaded still reads as its URL until the title arrives.
    struct Bookmark {
        std::string url;
        std::string title;
    };
    std::vector<Bookmark> const& bookmarks() const { return m_bookmarks; }
    bool is_bookmarked(std::string const& url) const;
    void add_bookmark(std::string url, std::string title);
    bool remove_bookmark(std::string const& url);
    // Adds when the page is not bookmarked, removes when it is. Returns true when the page is bookmarked afterwards.
    bool toggle_bookmark();

    // `history.pushState(data, unused, url)` and `history.replaceState(...)`. No document is loaded: the URL and the
    // history move, the page stays. Returns false when `url` is not same-origin with the current entry, which is the
    // case upstream throws a SecurityError for (HTML/History.cpp:169-223).
    bool push_state(std::string const& url, std::string serialized_state, bool replace);

    // The current entry's serialized state, empty when it has none.
    std::string const& current_state() const;

private:
    bool m_pending_scroll_restore { false };
    float m_pending_scroll_x { 0 };
    float m_pending_scroll_y { 0 };

public:
    bool history_go(int delta);
    bool history_back() { return history_go(-1); }
    bool history_forward() { return history_go(1); }

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
    std::vector<Bookmark> m_bookmarks;
    std::size_t m_active { 0 };
};

} // namespace aetheris::ui

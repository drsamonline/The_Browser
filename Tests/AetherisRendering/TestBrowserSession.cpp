// Tabs and navigation state, tested without rendering or networking.

#include <ui/browser_session.hpp>
#include <ui/library_page.hpp>

#include <cstdio>
#include <cstdlib>

using namespace aetheris::ui;

static void require(bool condition, char const* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

int main()
{
    // URL handling: bare hosts get a scheme, localhost gets http.
    {
        require(BrowserSession::normalise("example.com") == "https://example.com", "bare host gets https");
        require(BrowserSession::normalise("localhost:8080/x") == "http://localhost:8080/x", "localhost gets http");
        require(BrowserSession::normalise("https://a.test/b") == "https://a.test/b", "an existing scheme is kept");
        require(BrowserSession::normalise("  example.com  ") == "https://example.com", "surrounding space is trimmed");
        require(!BrowserSession::is_loadable(""), "an empty URL is not loadable");
        require(!BrowserSession::is_loadable("   "), "whitespace is not loadable");
        require(BrowserSession::is_loadable("example.com"), "a bare host is loadable");
    }

    // Opening a URL creates the first tab and shows a loading state.
    {
        BrowserSession session;
        require(session.tab_count() == 0, "no tabs before open");
        require(session.open("example.com"), "open succeeds");
        require(session.tab_count() == 1, "open creates a tab");
        require(session.current_url() == "https://example.com", "the URL is normalised");
        require(session.loading(), "the tab is loading after open");
        require(session.display_title() == "https://example.com", "the URL stands in for a missing title");

        session.set_page_title("Example Domain");
        require(session.display_title() == "Example Domain", "the page title is used when set");

        session.set_loading(false);
        require(!session.loading(), "loading clears");
    }

    // Navigation changes the URL and marks loading; a failure is recorded.
    {
        BrowserSession session;
        session.open("example.com");
        require(session.navigate("other.test/page"), "navigate succeeds");
        require(session.current_url() == "https://other.test/page", "navigate updates the URL");
        require(session.loading(), "navigate starts loading");

        session.set_error("connection refused");
        require(session.failed(), "the error is recorded");
        require(!session.loading(), "a failure stops loading");
        require(session.error() == "connection refused", "the message is kept");

        require(!session.navigate(""), "navigating to nothing is refused");
    }

    // Tabs: new, activate, close.
    {
        BrowserSession session;
        session.open("one.test");
        auto second = session.new_tab();
        require(session.tab_count() == 2, "new_tab adds a tab");
        require(second == 1 && session.active_index() == 1, "the new tab becomes active");
        require(session.display_title() == "New Tab", "an empty tab is called New Tab");

        session.navigate("two.test");
        require(session.current_url() == "https://two.test", "the second tab navigates independently");

        require(session.activate_tab(0), "the first tab can be activated");
        require(session.current_url() == "https://one.test", "each tab keeps its own URL");

        require(session.close_active_tab(), "closing works");
        require(session.tab_count() == 1, "one tab remains");
        require(session.active_index() == 0, "the active index is corrected");
        require(session.current_url() == "https://two.test", "the surviving tab is the right one");

        require(session.close_active_tab(), "the last tab can be closed");
        require(session.tab_count() == 0, "no tabs remain");
        require(session.display_title() == "New Tab", "an empty session shows New Tab");
        require(!session.activate_tab(5), "activating a missing tab fails");
    }

    std::printf("browser session: tabs and navigation verified\n");
// Row 1g4c: `history.scrollRestoration`. The mode is per history entry, as upstream keeps it on the session
    // history entry (HTML/History.cpp:229-242), and a traversal restores the scroll only when the mode is Auto.
    {
        BrowserSession session;
        require(session.open("https://a.test/"), "open a");
        require(session.scroll_restoration() == ScrollRestoration::Auto, "a new entry defaults to Auto");

        // Scroll a page, then leave it: the offset is recorded on the entry being left.
        session.record_scroll(0.0f, 320.0f);
        require(session.navigate("https://b.test/"), "navigate to b");
        float x = -1, y = -1;
        require(!session.take_scroll_restore(x, y), "a forward navigation restores nothing");

        // b is the entry being navigated *to* when we go forward, so its mode is the one that decides. Set it to
        // Manual there: the page will put its own scroll back.
        session.record_scroll(0.0f, 40.0f);
        session.set_scroll_restoration(ScrollRestoration::Manual);
        require(session.scroll_restoration() == ScrollRestoration::Manual, "the mode is set on the current entry");

        // Back to a: a is still Auto, so its scroll comes back, once.
        require(session.history_back(), "go back");
        require(session.take_scroll_restore(x, y), "Auto restores the scroll");
        require(x == 0.0f && y == 320.0f, "and it is the offset a was left at, not b's");
        require(!session.take_scroll_restore(x, y), "the restore is answered once, so a repaint cannot re-apply it");

        // Forward to b, which is Manual: nothing is restored, the page does it itself.
        require(session.history_forward(), "forward to b");
        float mx = -1, my = -1;
        require(!session.take_scroll_restore(mx, my), "Manual on the destination restores nothing");

        // And the mode is per entry: a kept its own while b was Manual.
        require(session.scroll_restoration() == ScrollRestoration::Manual, "b's mode is still what it was set to");
    }

// Row 1g4b: `pushState`/`replaceState`. The URL and the history move; nothing is loaded; a cross-origin URL is
    // refused, which is the SecurityError upstream throws (HTML/History.cpp:169-223).
    {
        BrowserSession session;
        require(session.open("https://a.test/start"), "open a page");
        session.set_loading(false); // the page has loaded; from here nothing should start a load again
        require(session.history_length() == 1, "one entry to begin with");
        require(session.current_state().empty(), "and no state on it");

        require(session.push_state("/two", "{\"n\":2}", false), "pushState to a same-origin path");
        require(session.history_length() == 2, "the history grew by one");
        require(session.current_url() == "https://a.test/two", "and the URL moved");
        require(session.current_state() == "{\"n\":2}", "the state is on the new entry");
        require(!session.loading(), "nothing was loaded: pushState is a history operation, not a navigation");

        require(session.push_state("", "{\"n\":3}", false), "an empty url keeps the current one");
        require(session.current_url() == "https://a.test/two", "so the URL is unchanged");
        require(session.history_length() == 3, "but a new entry was still added");

        require(session.push_state("https://b.test/away", "null", false) == false, "a cross-origin URL is refused");
        require(session.history_length() == 3, "and nothing was added");
        require(session.current_url() == "https://a.test/two", "nor did the URL move");

        require(session.push_state("/replaced", "{\"r\":1}", true), "replaceState");
        require(session.history_length() == 3, "replaces rather than grows");
        require(session.current_url() == "https://a.test/replaced", "the URL is replaced");
        require(session.current_state() == "{\"r\":1}", "and so is the state");

        // Going back reads the state of the entry it lands on, not the one it left.
        require(session.history_back(), "go back");
        require(session.current_url() == "https://a.test/two", "back to the second entry");
        require(session.current_state() == "{\"n\":2}", "with that entry's own state, not the one replaced above it");
        require(session.history_back(), "back again");
        require(session.current_state().empty(), "the first entry has no state");
    }

// Row 4.2: the history and bookmarks model, and the panel documents built from it.
    {
        BrowserSession session;
        require(session.open("https://a.test/one"), "open the first page");
        session.set_page_title("First");
        require(session.navigate("https://a.test/two"), "go to the second");
        session.set_page_title("Second");

        // Each entry keeps the title it had while it was current.
        auto const& entries = session.history_entries();
        require(entries.size() == 2, "two history entries");
        require(entries[0].title == "First", "the first kept its title");
        require(entries[1].title == "Second", "and so did the second");

        // Bookmarks: add, detect, refuse a duplicate, toggle, remove.
        require(!session.is_bookmarked("https://a.test/one"), "nothing is bookmarked to begin with");
        session.add_bookmark("https://a.test/one", "First");
        require(session.is_bookmarked("https://a.test/one"), "the bookmark is there");
        require(session.bookmarks().size() == 1, "one bookmark");
        session.add_bookmark("https://a.test/one", "First again");
        require(session.bookmarks().size() == 1, "adding the same URL twice keeps one");
        require(session.bookmarks()[0].title == "First", "and the first title it was given");

        require(session.toggle_bookmark(), "toggling the current page bookmarks it");
        require(session.is_bookmarked("https://a.test/two"), "which is the page the tab is on");
        require(!session.toggle_bookmark(), "toggling again removes it");
        require(!session.is_bookmarked("https://a.test/two"), "and it is gone");

        require(session.remove_bookmark("https://a.test/one"), "remove by URL");
        require(session.bookmarks().empty(), "nothing left");

        // The panels: real links, newest history first, and the current page marked.
        BrowserSession panel_session;
        panel_session.open("https://a.test/first");
        panel_session.set_page_title("First page");
        panel_session.navigate("https://a.test/second");
        panel_session.set_page_title("Second page");
        panel_session.add_bookmark("https://a.test/first", "First page");

        auto const history_html = LibraryPage::build(panel_session, LibraryPage::Mode::History);
        require(history_html.find("History") != std::string::npos, "the history panel is titled");
        require(history_html.find("<a href='https://a.test/first'") != std::string::npos,
            "an entry is a real link, so a click navigates through the ordinary path");
        require(history_html.find("Second page") < history_html.find("First page"),
            "newest first: the page you were just on is at the top");
        require(history_html.find("current page") != std::string::npos, "the current page is marked");
        require(history_html.find("\u2605") != std::string::npos, "a bookmarked entry carries a star");

        auto const bookmarks_html = LibraryPage::build(panel_session, LibraryPage::Mode::Bookmarks);
        require(bookmarks_html.find("Bookmarks") != std::string::npos, "the bookmarks panel is titled");
        require(bookmarks_html.find("https://a.test/first") != std::string::npos, "with the bookmark in it");
        require(bookmarks_html.find("https://a.test/second") == std::string::npos, "and nothing else");

        // An empty panel says so rather than rendering a blank page.
        BrowserSession empty;
        empty.open("https://a.test/only");
        auto const empty_bookmarks = LibraryPage::build(empty, LibraryPage::Mode::Bookmarks);
        require(empty_bookmarks.find("No bookmarks yet") != std::string::npos, "an empty bookmarks panel explains itself");
    }

    return 0;
}

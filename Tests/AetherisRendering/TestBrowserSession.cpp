// Tabs and navigation state, tested without rendering or networking.

#include <ui/browser_session.hpp>

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

    return 0;
}

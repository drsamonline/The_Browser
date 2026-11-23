#include "library_page.hpp"

namespace aetheris::ui {

namespace {

std::string escape(std::string const& value)
{
    std::string out;
    for (char c : value) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else out += c;
    }
    return out;
}

// A row: the title (or the URL when the page never gave one) over the URL, as a link.
std::string row(std::string const& url, std::string const& title, bool active, bool bookmarked)
{
    std::string heading = title.empty() ? url : title;
    std::string html = "<div style='padding:10px 16px;border-bottom:1px solid #e8eaed'>";
    html += "<a href='" + escape(url) + "' style='color:#1a73e8;text-decoration:none;font-size:14px'>";
    html += escape(heading);
    if (bookmarked)
        html += " \u2605"; // a star, so a bookmarked page is recognisable in the history list too
    html += "</a>";
    html += "<div style='color:#5f6368;font-size:12px;margin-top:2px'>" + escape(url);
    if (active)
        html += " \u2014 current page";
    html += "</div></div>";
    return html;
}

} // namespace

std::string LibraryPage::build(BrowserSession const& session, Mode mode)
{
    bool const history = mode == Mode::History;
    std::string html = "<html><body style='margin:0;background:#ffffff;color:#202124'>";
    html += "<div style='padding:18px 16px 6px 16px;font-size:20px'>";
    html += history ? "History" : "Bookmarks";
    html += "</div>";

    std::size_t shown = 0;
    if (history) {
        // Newest first: the page you were on a moment ago is the one you are most likely to want back.
        auto const& entries = session.history_entries();
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            html += row(it->url, it->title, it->url == session.current_url(), session.is_bookmarked(it->url));
            ++shown;
        }
    } else {
        for (auto const& bookmark : session.bookmarks()) {
            html += row(bookmark.url, bookmark.title, bookmark.url == session.current_url(),
                true);
            ++shown;
        }
    }

    if (shown == 0) {
        html += "<div style='padding:16px;color:#5f6368;font-size:13px'>";
        html += history ? "Nothing in this tab's history yet." : "No bookmarks yet. Press Ctrl+D on a page to add one.";
        html += "</div>";
    }
    html += "</body></html>";
    return html;
}

} // namespace aetheris::ui

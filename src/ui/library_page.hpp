#pragma once

#include "browser_session.hpp"

#include <string>

namespace aetheris::ui {

// The history and bookmarks panels, as documents. They are ordinary pages of real `<a href>` links, so a click goes
// through the same navigation path a page's own links use - there is no second hit-testing path to keep in step
// (row 4.2).
struct LibraryPage {
    enum class Mode {
        History,
        Bookmarks,
    };

    // The panel as HTML. `active_url` is marked, so a reader can see where they are in their own history.
    static std::string build(BrowserSession const& session, Mode mode);
};

} // namespace aetheris::ui

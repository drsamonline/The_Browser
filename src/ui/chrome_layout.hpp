#pragma once

#include <cstddef>
#include <vector>

namespace aetheris::ui {

// Where the chrome's pieces are. One place computes the geometry, and both the drawing and the hit-testing read it -
// a tab strip whose painted tabs and clickable tabs are computed twice drifts apart the first time a tab is added,
// which is exactly the bug this module exists to make impossible (row 4.1).
struct ChromeLayout {
    static constexpr int kHeight = 34;

    // The tab strip: each tab's box, in order.
    struct TabBox {
        std::size_t index { 0 };
        int left { 0 };
        int width { 0 };
    };
    std::vector<TabBox> tabs;
    int close_size { 12 }; // the "x" at the right of each tab, inside its box

    int new_tab_left { 0 };
    int new_tab_width { 20 };
    int back_left { 0 };
    int forward_left { 0 };
    int reload_left { 0 };
    int address_left { 0 };
    int address_width { 0 };

    enum class Hit {
        None,
        Tab,
        CloseTab,
        NewTab,
        Back,
        Forward,
        Reload,
        Address,
    };
    struct HitResult {
        Hit kind { Hit::None };
        std::size_t index { 0 };
    };

    // `tab_count` tabs with `active` selected, laid out inside `width` pixels.
    static ChromeLayout compute(std::size_t tab_count, std::size_t active, int width);

    // What a click at (x, y) means. A click in a tab's box switches to it, unless it lands on that tab's close
    // button, which closes it - the same two-target rule a browser's tab strip has.
    HitResult hit_test(int x, int y) const;

    // The close button of a tab, so drawing and hit-testing agree about where it is.
    int close_left(TabBox const& box) const { return box.left + box.width - close_size - 4; }

    // The left edge of the address bar's text area, which is where the strip's controls end.
    int controls_right() const { return address_left; }
};

} // namespace aetheris::ui

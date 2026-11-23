#include "chrome_layout.hpp"

#include <algorithm>

namespace aetheris::ui {

namespace {

// The buttons after the tab strip, in the order they are painted: +, back, forward, reload.
constexpr int kNewTabWidth = 20;
constexpr int kButtonWidth = 26;
constexpr int kStripMargin = 8;
constexpr int kAddressGap = 8;
constexpr int kMenuWidth = 30;
constexpr int kMinTabWidth = 56;
constexpr int kMaxTabWidth = 190;

} // namespace

ChromeLayout ChromeLayout::compute(std::size_t tab_count, std::size_t active, int width)
{
    (void)active; // the active tab is a drawing decision, not a geometry one - the box is the same size either way
    ChromeLayout layout;

    int const buttons = kNewTabWidth + kButtonWidth * 3;
    int const reserved = kStripMargin + buttons + kAddressGap + kMenuWidth + kStripMargin;
    int const available = std::max(0, width - reserved);

    std::size_t const count = std::max<std::size_t>(1, tab_count);
    int tab_width = kMaxTabWidth;
    if (static_cast<int>(count) * kMaxTabWidth > available)
        tab_width = std::max(kMinTabWidth, available / static_cast<int>(count));

    int left = kStripMargin;
    for (std::size_t index = 0; index < count; ++index) {
        layout.tabs.push_back(TabBox { index, left, tab_width });
        left += tab_width;
    }

    layout.new_tab_left = left;
    layout.new_tab_width = kNewTabWidth;
    left += kNewTabWidth;

    layout.back_left = left;
    left += kButtonWidth;
    layout.forward_left = left;
    left += kButtonWidth;
    layout.reload_left = left;
    left += kButtonWidth;

    layout.address_left = left + kAddressGap;
    layout.address_width = std::max(0, width - layout.address_left - kMenuWidth - kStripMargin);
    return layout;
}

ChromeLayout::HitResult ChromeLayout::hit_test(int x, int y) const
{
    if (y < 0 || y >= kHeight)
        return { Hit::None, 0 };

    for (auto const& box : tabs) {
        if (x < box.left || x >= box.left + box.width)
            continue;
        // The close button is inside the tab's box and wins the hit: a click there closes, it does not switch.
        if (x >= close_left(box))
            return { Hit::CloseTab, box.index };
        return { Hit::Tab, box.index };
    }

    if (x >= new_tab_left && x < new_tab_left + new_tab_width)
        return { Hit::NewTab, 0 };
    if (x >= back_left && x < back_left + kButtonWidth)
        return { Hit::Back, 0 };
    if (x >= forward_left && x < forward_left + kButtonWidth)
        return { Hit::Forward, 0 };
    if (x >= reload_left && x < reload_left + kButtonWidth)
        return { Hit::Reload, 0 };
    if (x >= address_left && x < address_left + address_width)
        return { Hit::Address, 0 };
    return { Hit::None, 0 };
}

} // namespace aetheris::ui

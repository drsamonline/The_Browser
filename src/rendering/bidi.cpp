#include "bidi.hpp"

#include "font.hpp" // utf8_next()

#include <algorithm>
#include <cstddef>

namespace aetheris::rendering {

namespace {

struct Range {
    uint32_t first;
    uint32_t last;
};

// R: strong right to left, left to right by no reading.
constexpr Range kRightToLeft[] {
    { 0x0590, 0x05FF }, // Hebrew
    { 0x07C0, 0x07FF }, // NKo
    { 0x0800, 0x083F }, // Samaritan
    { 0x200F, 0x200F }, // RIGHT-TO-LEFT MARK
    { 0xFB1D, 0xFB4F }, // Hebrew presentation forms
    { 0x10800, 0x10CFF }, // Cypriot, Kharoshthi, Phoenician and the other early RTL scripts
    { 0x10F00, 0x10FFF }, // Old Sogdian, Sogdian, Chorasmian, Elymaic
    { 0x1E800, 0x1E8DF }, // Mende Kikakui
};

// AL: strong right to left, joining.
constexpr Range kArabicLetter[] {
    { 0x0600, 0x06FF }, // Arabic (the digits inside it are handled before this table is consulted)
    { 0x0700, 0x074F }, // Syriac
    { 0x0750, 0x077F }, // Arabic Supplement
    { 0x0780, 0x07BF }, // Thaana
    { 0x0840, 0x085F }, // Mandaic
    { 0x0860, 0x086F }, // Syriac Supplement
    { 0x0870, 0x089F }, // Arabic Extended-B
    { 0x08A0, 0x08FF }, // Arabic Extended-A
    { 0xFB50, 0xFDFF }, // Arabic Presentation Forms-A
    { 0xFE70, 0xFEFF }, // Arabic Presentation Forms-B
    { 0x10D00, 0x10D3F }, // Hanifi Rohingya
    { 0x10EC0, 0x10EFF }, // Arabic Extended-C
    { 0x1E900, 0x1E95F }, // Adlam
    { 0x1EE00, 0x1EEFF }, // Arabic Mathematical Alphabetic Symbols
};

// EN and AN: weak, and displayed left to right even inside right-to-left text.
constexpr Range kNumber[] {
    { 0x0030, 0x0039 }, // ASCII digits
    { 0x0660, 0x0669 }, // ARABIC-INDIC DIGITs
    { 0x06F0, 0x06F9 }, // EXTENDED ARABIC-INDIC DIGITs
    { 0x10E60, 0x10E7E }, // Rumi numerals
};

template <std::size_t N>
bool in_ranges(uint32_t code_point, Range const (&ranges)[N])
{
    for (auto const& range : ranges) {
        if (code_point >= range.first && code_point <= range.last)
            return true;
    }
    return false;
}

} // namespace

BidiClass bidi_class(uint32_t code_point)
{
    if (code_point < 0x80) {
        // Latin letters are L, and everything else ASCII is neutral or a number. A 128-entry table
        // would be faster than the branches below and no easier to read; the gate above means this is
        // only reached for text that already contains a non-ASCII code point.
        if ((code_point >= 'a' && code_point <= 'z') || (code_point >= 'A' && code_point <= 'Z'))
            return BidiClass::LeftToRight;
        if (code_point >= '0' && code_point <= '9')
            return BidiClass::Number;
        return BidiClass::Neutral;
    }
    // Digits first: the Arabic-Indic digits live inside the Arabic block, and calling them AL would
    // put them in the RTL run, where HarfBuzz would reverse them.
    if (in_ranges(code_point, kNumber))
        return BidiClass::Number;
    if (in_ranges(code_point, kRightToLeft))
        return BidiClass::RightToLeft;
    if (in_ranges(code_point, kArabicLetter))
        return BidiClass::ArabicLetter;
    return BidiClass::LeftToRight;
}

bool may_require_bidi(std::string_view text)
{
    for (std::size_t index = 0; index < text.size();) {
        auto const code_point = utf8_next(text, index);
        // ASCII is never right to left, so a Latin page pays one byte test per character and nothing
        // else. Above ASCII the class decides: "café" and "١٢٣" are non-ASCII and still need no
        // split, which is why this asks the table rather than the byte value.
        if (code_point < 0x80)
            continue;
        auto const kind = bidi_class(code_point);
        if (kind == BidiClass::RightToLeft || kind == BidiClass::ArabicLetter)
            return true;
    }
    return false;
}

std::vector<DirectionalRun> split_directional_runs(std::string_view text)
{
    if (!may_require_bidi(text))
        return {};

    struct Item {
        std::size_t begin;
        std::size_t end;
        BidiClass kind;
    };

    std::vector<Item> items;
    for (std::size_t index = 0; index < text.size();) {
        auto const begin = index;
        auto const code_point = utf8_next(text, index);
        items.push_back(Item { begin, index, bidi_class(code_point) });
    }

    // One direction per code point.
    //
    // UAX#9 N1: a run of neutrals between two strong characters of the same direction takes it. N2 is
    // the other case - between different directions, or at an edge - and there the neutral takes the
    // paragraph's direction, which this engine has no way to set yet, so it is left to right. Numbers
    // count as left to right here because that is the run they are placed in: a neutral before a
    // number therefore leaves the right-to-left run, which is what keeps "سلام 123" from putting the
    // space on the wrong side of the digits.
    auto direction_of = [](BidiClass kind) {
        return kind == BidiClass::RightToLeft || kind == BidiClass::ArabicLetter;
    };

    std::vector<bool> rtl(items.size(), false);
    for (std::size_t i = 0; i < items.size();) {
        if (items[i].kind != BidiClass::Neutral) {
            rtl[i] = direction_of(items[i].kind);
            ++i;
            continue;
        }
        auto const begin = i;
        while (i < items.size() && items[i].kind == BidiClass::Neutral)
            ++i;
        auto const end = i; // [begin, end) is one run of neutrals

        bool const before = begin > 0 ? rtl[begin - 1] : false;
        bool const after = end < items.size() ? direction_of(items[end].kind) : false;
        bool const resolved = before && after;
        for (std::size_t j = begin; j < end; ++j)
            rtl[j] = resolved;
    }

    std::vector<DirectionalRun> runs;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (!runs.empty() && runs.back().right_to_left == rtl[i]) {
            runs.back().end = items[i].end;
        } else {
            runs.push_back(DirectionalRun { items[i].begin, items[i].end, rtl[i] });
        }
    }
    return runs;
}

} // namespace aetheris::rendering

namespace aetheris::rendering {

namespace {

// What an item is, before its level is decided. `Number` and `Neutral` have no direction of their own:
// the first is displayed left to right wherever it sits, the second takes its direction from the strong
// characters around it.
enum class ItemKind {
    RightToLeft,
    LeftToRight,
    Number,
    Neutral,
};

ItemKind kind_of(std::string_view item)
{
    bool right_to_left = false;
    bool left_to_right = false;
    bool number = false;
    for (std::size_t index = 0; index < item.size();) {
        switch (bidi_class(utf8_next(item, index))) {
        case BidiClass::RightToLeft:
        case BidiClass::ArabicLetter:
            right_to_left = true;
            break;
        case BidiClass::LeftToRight:
            left_to_right = true;
            break;
        case BidiClass::Number:
            number = true;
            break;
        case BidiClass::Neutral:
            break;
        }
    }
    // A fragment is one direction or it is not handled: "abcسلام" as a single item is right to left,
    // because one right-to-left character makes the run right to left. Splitting a fragment would need
    // layout to emit two fragments, which is what split_directional_runs() does inside the shaper.
    if (right_to_left)
        return ItemKind::RightToLeft;
    if (left_to_right)
        return ItemKind::LeftToRight;
    if (number)
        return ItemKind::Number;
    return ItemKind::Neutral;
}

} // namespace

std::vector<std::size_t> visual_order(std::vector<std::string_view> const& items, bool paragraph_right_to_left)
{
    std::vector<std::size_t> order(items.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    // The gate, per line rather than per string: a Latin line's items are all ASCII and it leaves here
    // without a table lookup or an allocation.
    bool any_right_to_left = false;
    for (auto const& item : items) {
        if (may_require_bidi(item)) {
            any_right_to_left = true;
            break;
        }
    }
    if (!any_right_to_left)
        return order;

    std::vector<ItemKind> kinds;
    kinds.reserve(items.size());
    for (auto const& item : items)
        kinds.push_back(kind_of(item));

    // The nearest strong direction before each item, which is what the weak types take their level
    // from (UAX#9 W rules, collapsed to "a number follows the direction of the text it sits in").
    int const paragraph_direction = paragraph_right_to_left ? 1 : 0;
    std::vector<int> strong_before(items.size(), paragraph_direction);
    int running = paragraph_direction;
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        strong_before[i] = running;
        if (kinds[i] == ItemKind::RightToLeft)
            running = 1;
        else if (kinds[i] == ItemKind::LeftToRight)
            running = 0;
    }

    // Directions first, levels second: a left-to-right item is level 0 in a left-to-right paragraph and
    // level 2 in a right-to-left one (UAX#9 I2), a right-to-left item is level 1 in both.
    std::vector<bool> right_to_left(items.size(), false);
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        switch (kinds[i]) {
        case ItemKind::RightToLeft:
            right_to_left[i] = true;
            break;
        case ItemKind::LeftToRight:
            right_to_left[i] = false;
            break;
        case ItemKind::Number:
            right_to_left[i] = false;
            break;
        case ItemKind::Neutral: {
            // N1: a neutral between two strong characters of one direction takes it. N2: anything else
            // takes the paragraph direction.
            std::size_t end = i;
            while (end < kinds.size() && kinds[end] == ItemKind::Neutral)
                ++end;
            int after = paragraph_direction;
            for (std::size_t j = end; j < kinds.size(); ++j) {
                if (kinds[j] == ItemKind::RightToLeft) {
                    after = 1;
                    break;
                }
                if (kinds[j] == ItemKind::LeftToRight) {
                    after = 0;
                    break;
                }
            }
            // N1 when both sides agree, N2 - the paragraph direction - when they do not. Collapsing N2 to
            // "left to right" is wrong for a right-to-left paragraph, and the test caught exactly that:
            // the two spaces of "abc سلام xyz" went to the ends of the line instead of staying with
            // their words.
            bool resolved = paragraph_right_to_left;
            if (strong_before[i] == 1 && after == 1)
                resolved = true;
            else if (strong_before[i] == 0 && after == 0)
                resolved = false;
            for (std::size_t j = i; j < end; ++j)
                right_to_left[j] = resolved;
            i = end - 1;
            break;
        }
        }
    }

    std::vector<int> levels(items.size(), 0);
    int highest = 0;
    for (std::size_t i = 0; i < right_to_left.size(); ++i) {
        levels[i] = right_to_left[i] ? 1 : (paragraph_right_to_left ? 2 : 0);
        highest = std::max(highest, levels[i]);
    }

    // L2: from the highest level down to the lowest odd level, reverse every contiguous run at or above
    // it. With levels 0..2 that is at most two passes.
    for (int level = highest; level >= 1; --level) {
        std::size_t i = 0;
        while (i < order.size()) {
            if (levels[order[i]] < level) {
                ++i;
                continue;
            }
            std::size_t begin = i;
            while (i < order.size() && levels[order[i]] >= level)
                ++i;
            std::reverse(order.begin() + static_cast<std::ptrdiff_t>(begin), order.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
    return order;
}

} // namespace aetheris::rendering

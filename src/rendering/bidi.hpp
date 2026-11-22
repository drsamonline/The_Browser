#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace aetheris::rendering {

// A text run can only be shaped in one direction: HarfBuzz takes one `hb_direction_t` per buffer, and
// guessing it from the buffer's first strong character is wrong the moment a string mixes scripts.
// Measured on "abc سلام xyz": guessed LTR, HarfBuzz returned 12 glyphs with the Arabic in isolated
// forms (clusters ascending); shaped as its own RTL run, the same Arabic is 3 glyphs (the lam-alef
// ligature forms) with clusters descending, in visual order. See docs/CHECKLIST.md row 2e.
//
// This is UAX#9 level 1 - classification and splitting. Level 2, resolving embedding levels and
// reordering runs into visual order, is not here: runs are concatenated in logical order, which is
// correct while there is at most one right-to-left segment on a line and wrong beyond that. Row 2e2.
enum class BidiClass : uint8_t {
    LeftToRight, // L
    RightToLeft, // R, including Hebrew and the RTL historic scripts listed below
    ArabicLetter, // AL: Arabic, Syriac, Thaana, NKo, and their presentation forms
    Number, // EN, AN: weak, and displayed left to right even inside RTL text
    Neutral, // everything else: spaces, punctuation, symbols, marks
};

// The class of one code point.
//
// A table, not the Unicode Character Database: the engine is standalone and LibUnicode is not built
// here, so the ranges cover the RTL scripts that appear in real pages - Hebrew, Arabic in all its
// forms, Syriac, Thaana, NKo, Samaritan, Mandaic, Adlam, Hanifi Rohingya - and treat everything else
// as left to right. An obscure historic RTL block outside this list classifies as LTR, which puts its
// text in the wrong run rather than crashing it.
BidiClass bidi_class(uint32_t code_point);

struct DirectionalRun {
    std::size_t begin { 0 }; // byte offset into the text this run came from
    std::size_t end { 0 };
    bool right_to_left { false };
};

// Splits UTF-8 `text` into runs that each have one direction, in logical order.
//
// Neutrals take the direction of the text before them (UAX#9 N1's same-direction case, which is the
// common one). Numbers are their own left-to-right runs so that digits inside RTL text keep their
// order: put them in the RTL run and HarfBuzz reverses them.
//
// Returns an empty vector when the text needs no bidi processing at all - the caller then shapes it as
// one run exactly as before, which is the path every ASCII string takes.
std::vector<DirectionalRun> split_directional_runs(std::string_view text);

// The order a line's items should be *placed* in, left to right: indices into `items`.
//
// This is UAX#9 level 2. Items are fragments - words and spaces - because that is what layout has, and
// the level of an item is decided by the strong characters inside it. Levels are 0, 1 and 2 only:
// there is no `unicode-bidi: embed|isolate|override` in this engine and no explicit embedding controls,
// so the rules that create levels above 2 never fire. A Latin paragraph puts right-to-left items at
// level 1; a right-to-left paragraph puts left-to-right items at level 2. Numbers are left to right in
// both, which is what keeps digits inside Arabic text in their own order.
//
// Returns the identity order when nothing on the line is right to left, which is every Latin line.
std::vector<std::size_t> visual_order(std::vector<std::string_view> const& items, bool paragraph_right_to_left);

// Whether any code point in `text` could change a run's direction. Upstream's gate is
// `Unicode::may_require_bidi_processing()` (Libraries/LibUnicode/Bidi.cpp) and it is a gate, not a
// courtesy: the answer is no for ASCII, so a Latin page never pays for the table below.
bool may_require_bidi(std::string_view text);

} // namespace aetheris::rendering

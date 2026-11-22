// Does the engine split text where the direction changes, and does it shape each part in its own
// direction?
//
// The failure this pins, measured before the code existed: "abc سلام xyz" through one buffer guessed
// left to right, so HarfBuzz returned 12 glyphs with the Arabic in isolated forms. Shaped as its own
// right-to-left run the same Arabic is fewer glyphs with clusters descending - the lam-alef ligature
// formed - which is what a reader sees as joined letters.
//
// The split itself is a pure function, so most of this test needs no font at all.
#include "bidi.hpp"
#include "embedded_font.inc"
#include <shaper.hpp>
#include <text_layout.hpp>
#include <ttf_font.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace aetheris::rendering;

static void require(bool condition, char const* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

// Byte ranges, written as text, so a failure says which slice was wrong rather than which integer.
static std::string describe(std::string_view text, std::vector<DirectionalRun> const& runs)
{
    std::string out;
    for (auto const& run : runs) {
        if (!out.empty())
            out += ' ';
        out += run.right_to_left ? "rtl[" : "ltr[";
        out += std::string(text.substr(run.begin, run.end - run.begin));
        out += ']';
    }
    return out;
}

// The visual order, written as the items themselves, so a failure reads like the line.
static std::string describe_order(std::vector<std::string_view> const& items, std::vector<std::size_t> const& order)
{
    std::string out;
    for (auto index : order) {
        if (!out.empty())
            out += '|';
        out += items[index];
    }
    return out;
}

static void require_order(std::vector<std::string_view> const& items, bool paragraph_right_to_left, char const* expected, char const* message)
{
    auto const actual = describe_order(items, visual_order(items, paragraph_right_to_left));
    if (actual != expected) {
        std::cerr << "FAILED: " << message << "\n  expected: " << expected << "\n  actual:   " << actual << '\n';
        std::exit(1);
    }
}

static void require_runs(std::string_view text, char const* expected, char const* message)
{
    auto const actual = describe(text, split_directional_runs(text));
    if (actual != expected) {
        std::cerr << "FAILED: " << message << "\n  expected: " << expected << "\n  actual:   " << actual << '\n';
        std::exit(1);
    }
}

int main()
{
    // The gate. Upstream asks the same question for the same reason: a Latin page must not pay for the
    // classification table, and "no right-to-left code point" is decided by looking at the bytes.
    require(!may_require_bidi("hello world"), "ASCII needs no bidi processing");
    require(!may_require_bidi("caf\xc3\xa9"), "a Latin-1 accented letter is not right to left");
    require(may_require_bidi("\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d"), "Hebrew needs bidi processing");
    require(may_require_bidi("\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85"), "Arabic needs bidi processing");
    require(split_directional_runs("hello world").empty(), "and the split returns nothing for it, so the one-run path is kept");

    // One direction per run, and the boundaries land where the script changes.
    require_runs("\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85", "rtl[\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85]", "Arabic alone is one right-to-left run");
    require_runs("\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d", "rtl[\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d]", "Hebrew alone is one right-to-left run");
    require_runs("abc \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 xyz",
        "ltr[abc ] rtl[\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85] ltr[ xyz]",
        "Latin, Arabic, Latin is three runs and the spaces travel with the text they follow");
    require_runs("abc\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85", "ltr[abc] rtl[\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85]",
        "no space needed: the script change alone starts a run");
    require_runs("\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85",
        "rtl[\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85]",
        "a space between two Arabic words stays inside the right-to-left run (N1, same direction)");
    require_runs("\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 123", "rtl[\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85] ltr[ 123]",
        "digits are their own left-to-right run, so they are not reversed");
    require_runs("\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 \xd9\xa1\xd9\xa2\xd9\xa3",
        "rtl[\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85] ltr[ \xd9\xa1\xd9\xa2\xd9\xa3]",
        "Arabic-Indic digits are numbers too, not Arabic letters");
    require(split_directional_runs("\xd9\xa1\xd9\xa2\xd9\xa3").empty(),
        "a string of Arabic-Indic digits alone needs no split either");

// Level 2: the order the line's words are *placed* in. The shaper gets each word right; this is what
    // makes the words themselves land in the order a reader expects.
    {
        std::vector<std::string_view> const arabic_pair { "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85", " ",
            "\xd8\xb9\xd9\x84\xd9\x8a\xd9\x83\xd9\x85" };
        // Two Arabic words: the first one ends up on the right, whichever way the paragraph reads.
        require_order(arabic_pair, false, "\xd8\xb9\xd9\x84\xd9\x8a\xd9\x83\xd9\x85| |\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85",
            "a Latin paragraph places the first Arabic word on the right of the RTL run");
        require_order(arabic_pair, true, "\xd8\xb9\xd9\x84\xd9\x8a\xd9\x83\xd9\x85| |\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85",
            "a right-to-left paragraph does the same");

        std::vector<std::string_view> const mixed { "abc", " ", "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85", " ", "xyz" };
        require_order(mixed, false, "abc| |\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85| |xyz",
            "one Arabic word between Latin words keeps its place");
        require_order(mixed, true, "xyz| |\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85| |abc",
            "in a right-to-left paragraph the Latin ends swap around it");

        std::vector<std::string_view> const latin { "abc", " ", "xyz" };
        require_order(latin, false, "abc| |xyz", "a Latin line is untouched");
        require_order(latin, true, "abc| |xyz",
            "an all-Latin line in a right-to-left paragraph is reversed twice and comes back as it was");

        std::vector<std::string_view> const with_number { "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85", " ", "123" };
        require_order(with_number, false, "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85| |123",
            "in a Latin paragraph the digits follow the Arabic");
        require_order(with_number, true, "123| |\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85",
            "in a right-to-left paragraph the digits move to the left, in their own order");

        std::vector<std::string_view> const ascii_only { "hello" };
        require_order(ascii_only, false, "hello", "a single Latin word needs no reordering");
    }

    // Shaping. The embedded font is a Latin subset, so Arabic comes back as .notdef glyphs - but the
    // order of the clusters is the direction, and that is what a layout consumes.
    TtfFont font;
    std::vector<uint8_t> data;
    data.assign(kEmbeddedFontData, kEmbeddedFontData + sizeof(kEmbeddedFontData));
    require(font.load(data), "the embedded font loads");

    auto const ascii = shape_run(font.data(), "abc", 16.0f, font.identity());
    require(ascii.size() == 3 && ascii[0].cluster == 0 && ascii[2].cluster == 2,
        "left-to-right text keeps ascending clusters");

    auto const mixed = shape_run(font.data(), "abc \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85", 16.0f, font.identity());
    require(!mixed.empty(), "the mixed string shapes");
    bool seen_rtl_cluster = false;
    uint32_t last_rtl_cluster = 0;
    for (auto const& glyph : mixed) {
        if (glyph.cluster < 4)
            continue; // the Latin run
        if (seen_rtl_cluster)
            require(glyph.cluster < last_rtl_cluster, "the Arabic run comes back in visual order: clusters descend");
        seen_rtl_cluster = true;
        last_rtl_cluster = glyph.cluster;
    }
    require(seen_rtl_cluster, "the Arabic part of the mixed string is shaped at all");
    require(mixed.front().cluster == 0, "the Latin run still starts the string, in logical order");

    // One right-to-left run through the splitter must equal one right-to-left buffer: the splitter is
    // not allowed to change a string that was already one direction.
    auto const arabic_only = shape_run(font.data(), "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85", 16.0f, font.identity());
    require(!arabic_only.empty() && arabic_only.front().cluster > arabic_only.back().cluster,
        "a right-to-left string shapes in descending cluster order");

// The layout integration, which is the point of level 2: the fragments a line produces are the order
    // paint draws them in, so the first logical word has to be the *last* fragment on the line.
    {
        StyleProperties style;
        style.set("font-size", "16px");
        auto const fragments = TextLayout::layout(
            "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 \xd8\xb9\xd9\x84\xd9\x8a\xd9\x83\xd9\x85", 0, 0, 200, style);
        require(fragments.size() == 2, "two Arabic words produce two fragments");
        require(fragments[0].text == "\xd8\xb9\xd9\x84\xd9\x8a\xd9\x83\xd9\x85",
            "the second logical word is the first fragment");
        require(fragments[1].text == "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85",
            "and the first logical word is the last fragment");
        require(fragments[0].rect.x < fragments[1].rect.x, "fragments come back in the order they are drawn");
    }

    std::cout << "bidi: " << describe("abc \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 xyz", split_directional_runs("abc \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 xyz"))
              << ", and the Arabic run shapes in visual order\n";
    return 0;
}

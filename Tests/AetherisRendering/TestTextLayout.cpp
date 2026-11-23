#include <render_document.hpp>
#include <font.hpp>
#include <text_layout.hpp>

#include <layout.hpp>
#include <functional>
#include <string>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace aetheris::rendering;

static void require(bool condition, char const* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    require(TextLayout::collapse_whitespace("  hello \n\t world  ") == "hello world", "collapse whitespace");

    // UTF-8 decoding. Text used to be iterated a byte at a time, so every non-ASCII character was
    // measured and drawn as two to four Latin-1 glyphs.
    {
        struct DecodeCase {
            char const* text;
            uint32_t expected;
            std::size_t consumed;
        };
        DecodeCase const cases[] {
            { "a", 'a', 1 },
            { "\xC3\xA9", 0x00E9, 2 },        // é, two bytes
            { "\xE2\x82\xB9", 0x20B9, 3 },   // ₹, three bytes
            { "\xE6\x97\xA5", 0x65E5, 3 },   // 日
            { "\xF0\x9F\x98\x80", 0x1F600, 4 }, // 😀, four bytes
        };
        for (auto const& test : cases) {
            std::string_view const text(test.text);
            std::size_t index = 0;
            auto const code_point = utf8_next(text, index);
            require(code_point == test.expected, "a code point decodes to itself");
            require(index == test.consumed, "the decoder consumes exactly its bytes");
        }
        // Malformed and truncated input substitutes U+FFFD and always advances.
        {
            std::string_view const truncated("\xC3");
            std::size_t index = 0;
            require(utf8_next(truncated, index) == 0xFFFD, "a truncated sequence is U+FFFD");
            require(index == truncated.size(), "a truncated sequence still advances");
            std::string_view const malformed("\xC3(");
            index = 0;
            require(utf8_next(malformed, index) == 0xFFFD, "a bad continuation is U+FFFD");
            require(index == 1, "a bad continuation advances past one byte");
        }
    }

    // Measurement counts code points, not bytes: two é are two glyphs.
    {
        StyleProperties style;
        style.set("font-size", "16px");
        auto const width_of_two_e_acute = TextLayout::measure_text("\xC3\xA9\xC3\xA9", 1000.0f, style);
        auto const width_of_one_e_acute = TextLayout::measure_text("\xC3\xA9", 1000.0f, style);
        auto const width_of_four_ascii = TextLayout::measure_text("aaaa", 1000.0f, style);
        require(width_of_one_e_acute > 0.0f, "a non-ASCII character has a width");
        require(std::abs(width_of_two_e_acute - 2.0f * width_of_one_e_acute) < 0.01f,
            "two code points measure as twice one");
        // The discriminating assertion: four bytes of UTF-8 must not measure like four characters.
        require(width_of_two_e_acute < width_of_four_ascii,
            "two é measure narrower than four ASCII characters");
    }

    StyleProperties style;
    style.set("font-size", "10px");
    style.set("line-height", "2");
    auto fragments = TextLayout::layout("one two three four five", 0, 0, 25, style);
    require(fragments.size() >= 5, "word fragments");
    require(fragments[1].rect.y >= fragments[0].rect.y, "ordered lines");

    auto rendered = RenderDocument::create(
        "<div>Hello wonderful rendering engine world</div>",
        "div { width: 60px; font-size: 10px; line-height: 2; }",
        200);

    size_t text_commands = 0;
    for (auto const& command : rendered.render_tree().commands()) {
        if (command.type == PaintCommand::Type::DrawText) {
            ++text_commands;
            require(!command.text.empty(), "painted text");
            require(command.baseline >= command.rect.y, "baseline");
        }
    }
    require(text_commands > 1, "wrapped text creates fragments");
// Row 3i: when there is no break opportunity the text overflows rather than wrapping. The row claimed the
    // opposite ("ours never overflows"); measured, the engine already does what the spec asks, so these
    // assertions pin it rather than change it. The bitmap fallback makes the widths exact: 10 px per character.
    {
        StyleProperties style;
        style.set("font-size", "16px");

        auto const too_narrow = TextLayout::layout("aaa", 0, 0, 25, style);
        require(too_narrow.size() == 1, "one word with no opportunity stays on one line");
        require(too_narrow[0].rect.width > 25, "and it overflows the line rather than being squeezed");

        auto const long_word = TextLayout::layout("aaaaaaaa", 0, 0, 30, style);
        require(long_word.size() == 1, "a word wider than the line is not broken");
        require(long_word[0].rect.width > 30, "it overflows");

        auto const at_the_space = TextLayout::layout("aaa bbb", 0, 0, 30, style);
        require(at_the_space.size() == 2, "a space is a break opportunity, so the second word wraps");
        require(at_the_space[1].rect.y > at_the_space[0].rect.y, "onto the next line");

        // The same rule for inline boxes: no whitespace between them means no opportunity, so they stay on one
        // line and overflow, and a space between them is an opportunity and moves the second one down.
        auto spans = [](std::string const& separator) {
            return "<div style='width:100px'>"
                   "<span style='display:inline-block;width:60px;height:10px'>a</span>" + separator
                + "<span style='display:inline-block;width:60px;height:10px'>b</span></div>";
        };
        auto inline_rows = [](std::string const& html) {
            auto document = RenderDocument::create(html, "", 400);
            std::vector<float> ys;
            std::function<void(LayoutNode const&)> walk = [&](LayoutNode const& node) {
                if (node.display == LayoutDisplay::InlineBlock && node.dom_node && node.dom_node->name == "span")
                    ys.push_back(node.rect.y);
                for (auto const& child : node.children)
                    walk(*child);
            };
            walk(document.layout_root());
            return ys;
        };
        auto const without_space = inline_rows(spans(""));
        require(without_space.size() == 2, "two inline boxes are laid out");
        require(std::abs(without_space[0] - without_space[1]) < 0.01f,
            "no whitespace between them: no opportunity, so both stay on one line and overflow");
        auto const with_space = inline_rows(spans(" "));
        require(with_space.size() == 2, "two inline boxes are laid out with a space too");
        require(with_space[1] > with_space[0], "a space between them is an opportunity, so the second wraps");
    }

    return 0;
}

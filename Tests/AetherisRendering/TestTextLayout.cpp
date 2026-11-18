#include <render_document.hpp>
#include <font.hpp>
#include <text_layout.hpp>

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
    return 0;
}

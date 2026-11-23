#include "text_layout.hpp"
#include "bidi.hpp"
#include "css_units.hpp"
#include "font.hpp"
#include "font_library.hpp"
#include "shaper.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
namespace aetheris::rendering {
std::string TextLayout::collapse_whitespace(std::string_view text,bool preserve){ if(preserve) return std::string(text); std::string result; bool pending=false; for(unsigned char c:text){ if(std::isspace(c)){ pending=!result.empty(); continue;} if(pending){result+=' ';pending=false;} result+=char(c);} return result; }
float TextLayout::font_size(StyleProperties const& style){ return Font::from_style(style).metrics().size; }
float TextLayout::line_height(StyleProperties const& style){ auto size=font_size(style); auto value=style.get("line-height"); if(!value||*value=="normal") return size*1.2f; char* end=nullptr; auto parsed=std::strtof(value->c_str(),&end); if(end==value->c_str()) return size*1.2f; if(end&&*end=='\0') return parsed*size; return parsed; }
namespace {

// Whether the page asked for a word to be broken rather than allowed to overflow. Browsers overflow a word
// that cannot fit a line; `overflow-wrap: break-word|anywhere` and `word-break: break-all` ask for the
// other behaviour, and that is the only case this file breaks inside a word (row 2f).
bool allows_breaking_inside_words(StyleProperties const& style)
{
    if (auto const wrap = style.get("overflow-wrap"); wrap && (*wrap == "break-word" || *wrap == "anywhere"))
        return true;
    if (auto const word_break = style.get("word-break"); word_break && (*word_break == "break-all" || *word_break == "break-word"))
        return true;
    return false;
}

// Splits a word that cannot fit a line of its own into pieces that each fit, cutting only where the shaper
// says a break is safe. A combining mark, a ligature and a kerned pair are never cut, because none of them
// offers an offset inside itself. Without a font there are no clusters, so the word stays whole.
std::vector<std::string> break_word(std::string_view word, float available_width, float size, StyleProperties const& style)
{
    std::vector<std::string> pieces;
    auto const* font = active_font();
    if (!font) {
        pieces.emplace_back(word);
        return pieces;
    }
    auto const offsets = safe_break_offsets(font->data(), word, size, font->identity());
    if (offsets.size() < 2) {
        pieces.emplace_back(word);
        return pieces;
    }

    std::size_t start = 0;
    while (start < word.size()) {
        // The furthest offset that still fits. Offsets ascend, so the first one that does not fit ends the
        // search: everything after it is wider still.
        std::size_t best = 0;
        for (auto const offset : offsets) {
            if (offset <= start)
                continue;
            if (TextLayout::measure_text(word.substr(start, offset - start), size, style) > available_width)
                break;
            best = offset;
        }
        if (best == 0) {
            // Not even the first cluster fits. Emit it anyway: a line has to hold something, and the loop
            // must always advance or a word narrower than one cluster would hang the layout.
            for (auto const offset : offsets) {
                if (offset > start) {
                    best = offset;
                    break;
                }
            }
        }
        pieces.emplace_back(word.substr(start, best - start));
        start = best;
    }
    return pieces;
}

} // namespace

float TextLayout::measure_text(std::string_view text,float,StyleProperties const& style){ auto font=Font::from_style(style); auto letter=css_length_px(style.get("letter-spacing"),font.metrics().size,0.0f); auto word=css_length_px(style.get("word-spacing"),font.metrics().size,0.0f);
    // The width the pen will draw, not the sum of the code points' advances. Those are not the same: the
    // shaped width carries kerning, and it carries the ligature that turns two characters into one glyph.
    // Summing advances skipped kerning entirely, so a fragment's box was up to 2.7 px wide of its text at
    // 16 px ("To" 19.5625 px against 16.8594 px shaped) and the next fragment started where the glyphs
    // were not (row 2h). Font::measure() is the shaped path and the fallback for builds without a shaper.
    float width=font.measure(text,letter,word);
    // Spacing is not added here: the shaper applies it to the glyph advances (row 2g), so the width the
    // pen advances by and the width layout measures are one number rather than two that have to be kept
    // in step. Iterating bytes here made every non-ASCII character two to four glyphs wide.
    return std::max(0.0f,width); }
std::vector<TextFragment> TextLayout::layout(std::string_view source, float x, float y, float available_width, StyleProperties const& style)
{
    std::vector<TextFragment> out;
    auto const ws = style.get("white-space");
    bool const pre = ws && (*ws == "pre" || *ws == "pre-wrap");
    bool const nowrap = ws && (*ws == "nowrap" || *ws == "pre");
    bool const prewrap = ws && *ws == "pre-wrap";
    auto const text = collapse_whitespace(source, pre);
    if (text.empty() || available_width <= 0)
        return out;
    auto const size = font_size(style);
    auto const h = line_height(style);

    // Explicit breaks first, then tokens: a run of non-space characters is one token, and a space is
    // its own token only in `pre`, where it is part of the text rather than a separator.
    std::vector<std::vector<std::string>> lines(1);
    std::string token;
    auto flush = [&] {
        if (!token.empty()) {
            lines.back().push_back(token);
            token.clear();
        }
    };
    for (char c : text) {
        if (c == '\n' && pre) {
            flush();
            lines.emplace_back();
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) && !pre) {
            flush();
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) && pre) {
            flush();
            lines.back().push_back(" ");
            continue;
        }
        token += c;
    }
    flush();

    // Wrap. A line that is already full takes the next word on the next line; `nowrap` and `pre-wrap`
    // never break here.
    std::vector<std::vector<std::string>> wrapped;
    for (auto const& raw : lines) {
        std::vector<std::string> line;
        float w = 0;
        for (auto const& word : raw) {
            float ww = measure_text(word, size, style);
            float sp = line.empty() ? 0 : measure_text(" ", size, style);
            if (!nowrap && !prewrap && !line.empty() && w + sp + ww > available_width) {
                wrapped.push_back(line);
                line.clear();
                w = 0;
                sp = 0;
            }
            // A word that does not fit a line of its own. Browsers leave it overflowing; the properties
            // above ask for it to be broken instead, at offsets the shaper says are safe.
            if (!nowrap && !prewrap && line.empty() && ww > available_width && allows_breaking_inside_words(style)) {
                auto const pieces = break_word(word, available_width, size, style);
                if (pieces.size() > 1) {
                    for (std::size_t index = 0; index + 1 < pieces.size(); ++index)
                        wrapped.push_back({ pieces[index] });
                    w = measure_text(pieces.back(), size, style);
                    line.push_back(pieces.back());
                    continue;
                }
            }
            if (!line.empty() && word != " ") {
                line.push_back(" ");
                w += sp;
            }
            line.push_back(word);
            w += ww;
        }
        wrapped.push_back(line);
    }

    // Place the fragments. The line's total width is the same whatever order its words end up in, so the
    // alignment offset is computed once and the visual order only decides which fragment gets which slot
    // - which is what UAX#9 level 2 asks for (bidi.cpp, visual_order()).
    // `text-align` resolves against the block's direction (CSS/Properties.json: initial `start`; the
    // mapping is the one upstream writes out for canvas, LibWeb/HTML/Canvas/Canvas2DContextBase.cpp:445):
    // `start` is the left edge in a left-to-right block and the right edge in a right-to-left one, `end`
    // is the other way round, and `left`/`right` ignore direction. An unset value is `start`.
    auto const align = style.get("text-align");
    auto const direction = style.get("direction");
    bool const paragraph_right_to_left = direction && *direction == "rtl";
    bool const centred = align && *align == "center";
    bool align_to_right = paragraph_right_to_left; // `start`, and the default
    if (align && *align == "right")
        align_to_right = true;
    else if (align && *align == "left")
        align_to_right = false;
    else if (align && *align == "end")
        align_to_right = !paragraph_right_to_left;
    else if (centred)
        align_to_right = false;
    float cy = y;
    for (auto const& line : wrapped) {
        std::vector<float> widths;
        widths.reserve(line.size());
        float width = 0;
        for (auto const& t : line) {
            float const w = measure_text(t, size, style);
            widths.push_back(w);
            width += w;
        }
        float cx = x;
        if (centred)
            cx = x + std::max(0.f, (available_width - width) / 2);
        else if (align_to_right)
            cx = x + std::max(0.f, available_width - width);

        std::vector<std::string_view> items;
        items.reserve(line.size());
        for (auto const& t : line)
            items.push_back(t);
        auto const order = visual_order(items, paragraph_right_to_left);

        for (auto const index : order) {
            if (line[index] != " ")
                out.push_back({ line[index], { cx, cy, widths[index], h }, cy + size });
            cx += widths[index];
        }
        cy += h;
    }
    return out;
}
} // namespace aetheris::rendering

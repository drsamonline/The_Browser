#include "text_layout.hpp"
#include "bidi.hpp"
#include "font.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
namespace aetheris::rendering {
namespace { float parse_px(std::string const* value,float fallback){ if(!value||value->empty()||*value=="normal") return fallback; char* end=nullptr; auto parsed=std::strtof(value->c_str(),&end); return end!=value->c_str()?parsed:fallback; } }
std::string TextLayout::collapse_whitespace(std::string_view text,bool preserve){ if(preserve) return std::string(text); std::string result; bool pending=false; for(unsigned char c:text){ if(std::isspace(c)){ pending=!result.empty(); continue;} if(pending){result+=' ';pending=false;} result+=char(c);} return result; }
float TextLayout::font_size(StyleProperties const& style){ return Font::from_style(style).metrics().size; }
float TextLayout::line_height(StyleProperties const& style){ auto size=font_size(style); auto value=style.get("line-height"); if(!value||*value=="normal") return size*1.2f; char* end=nullptr; auto parsed=std::strtof(value->c_str(),&end); if(end==value->c_str()) return size*1.2f; if(end&&*end=='\0') return parsed*size; return parsed; }
float TextLayout::measure_text(std::string_view text,float,StyleProperties const& style){ auto font=Font::from_style(style); auto letter=parse_px(style.get("letter-spacing"),0); auto word=parse_px(style.get("word-spacing"),0);
    // The width the pen will draw, not the sum of the code points' advances. Those are not the same: the
    // shaped width carries kerning, and it carries the ligature that turns two characters into one glyph.
    // Summing advances skipped kerning entirely, so a fragment's box was up to 2.7 px wide of its text at
    // 16 px ("To" 19.5625 px against 16.8594 px shaped) and the next fragment started where the glyphs
    // were not (row 2h). Font::measure() is the shaped path and the fallback for builds without a shaper.
    float width=font.measure(text);
    // Spacing is per character, not per glyph: upstream applies letter-spacing once per cluster
    // (LibGfx/TextLayout.cpp, measure_text_width) and word-spacing once per word separator. Iterating
    // bytes made every non-ASCII character two to four glyphs wide.
    for(std::size_t index=0;index<text.size();){ auto const code_point=utf8_next(text,index); if(code_point==' ') width+=word; width+=letter;}
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
    auto const align = style.get("text-align");
    auto const direction = style.get("direction");
    bool const paragraph_right_to_left = direction && *direction == "rtl";
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
        if (align && *align == "center")
            cx = x + std::max(0.f, (available_width - width) / 2);
        else if (align && (*align == "right" || *align == "end"))
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

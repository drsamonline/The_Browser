#include "font.hpp"
#include "shaper.hpp"

#include "css_units.hpp"
#include "font_library.hpp"
#include "bitmap_font.hpp"
#include <cctype>
#include <cstdlib>
namespace aetheris::rendering { static float num(std::string const* v,float f){if(!v)return f;char*e=nullptr;float x=strtof(v->c_str(),&e);return e==v->c_str()?f:x;} float FontMetrics::advance_for_code_point(uint32_t code_point) const {
    if (auto* font = face) {
        auto glyph = font->glyph_index(code_point);
        float scale = size / static_cast<float>(font->units_per_em() > 0 ? font->units_per_em() : 1000);
        return static_cast<float>(font->advance_width(glyph)) * scale;
    }
    // The embedded bitmap font is ASCII art: anything above it has no cell of its own.
    return bitmap_advance(size);
} float FontMetrics::advance(char c) const { return advance_for_code_point(static_cast<unsigned char>(c)); } Font Font::from_style(StyleProperties const&s){Font f;f.m_metrics.size=css_length_px(s.get("font-size"),16.0f,16.0f);f.m_metrics.ascent=f.m_metrics.size*.8f;f.m_metrics.descent=f.m_metrics.size*.2f;f.m_metrics.line_gap=f.m_metrics.size*.2f;f.m_weight=num(s.get("font-weight"),400);
    // `font-family` names the face; a name no `@font-face` declared falls back to the engine's own font.
    f.m_metrics.face=font_for_family(s.get("font-family")?std::string_view(*s.get("font-family")):std::string_view{});
    if(!f.m_metrics.face) f.m_metrics.face=active_font();
    return f;} float Font::measure(std::string_view v,float letter_spacing,float word_spacing)const{
    // Shaped when possible: a ligature is one glyph with one advance, and summing per code point
    // measures it as two. Kerning, mark positioning and Arabic joining are the same argument. The
    // fallback below is the pre-shaping behaviour and is kept for builds without a shaper.
    if (auto* shaped_font = m_metrics.face; shaped_font && shaping_available()) {
        auto const glyphs = shape_run(shaped_font->data(), v, m_metrics.size, shaped_font->identity(), letter_spacing, word_spacing);
        if (!glyphs.empty()) {
            float total = 0;
            for (auto const& glyph : glyphs)
                total += glyph.advance;
            return total;
        }
    }
    float n=0;
    auto* font=m_metrics.face;
    unsigned previous=0;
    bool have_previous=false;
    for(std::size_t index=0;index<v.size();){
        auto const code_point=utf8_next(v,index);
        n+=m_metrics.advance_for_code_point(code_point)+letter_spacing;
        if(code_point==' '||code_point==0xA0||code_point=='\n') n+=word_spacing;
        if(font){
            auto glyph=font->glyph_index(code_point);
            if(have_previous){
                float scale=m_metrics.size/static_cast<float>(font->units_per_em()>0?font->units_per_em():1000);
                n+=static_cast<float>(kerning_adjustment(*font,static_cast<uint16_t>(previous),glyph))*scale;
            }
            previous=glyph;
            have_previous=true;
        }
    }
    return n;
} }

#include "font.hpp"

#include "font_library.hpp"
#include "bitmap_font.hpp"
#include <cctype>
#include <cstdlib>
namespace aetheris::rendering { static float num(std::string const* v,float f){if(!v)return f;char*e=nullptr;float x=strtof(v->c_str(),&e);return e==v->c_str()?f:x;} float FontMetrics::advance(char c) const {
    if (auto* font = active_font()) {
        auto glyph = font->glyph_index(static_cast<unsigned char>(c));
        float scale = size / static_cast<float>(font->units_per_em() > 0 ? font->units_per_em() : 1000);
        return static_cast<float>(font->advance_width(glyph)) * scale;
    }
    return bitmap_advance(size);
} Font Font::from_style(StyleProperties const&s){Font f;f.m_metrics.size=num(s.get("font-size"),16);f.m_metrics.ascent=f.m_metrics.size*.8f;f.m_metrics.descent=f.m_metrics.size*.2f;f.m_metrics.line_gap=f.m_metrics.size*.2f;f.m_weight=num(s.get("font-weight"),400);return f;} float Font::measure(std::string_view v)const{
    float n=0;
    auto* font=active_font();
    unsigned previous=0;
    bool have_previous=false;
    for(unsigned char c:v){
        n+=m_metrics.advance(static_cast<char>(c));
        if(font){
            auto glyph=font->glyph_index(c);
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

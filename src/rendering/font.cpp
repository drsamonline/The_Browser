#include "font.hpp"
#include <cctype>
#include <cstdlib>
namespace aetheris::rendering { static float num(std::string const* v,float f){if(!v)return f;char*e=nullptr;float x=strtof(v->c_str(),&e);return e==v->c_str()?f:x;} float FontMetrics::advance(char c) const {if(c==' ')return size*.33f;if(std::ispunct((unsigned char)c))return size*.45f;return size*.60f;} Font Font::from_style(StyleProperties const&s){Font f;f.m_metrics.size=num(s.get("font-size"),16);f.m_metrics.ascent=f.m_metrics.size*.8f;f.m_metrics.descent=f.m_metrics.size*.2f;f.m_metrics.line_gap=f.m_metrics.size*.2f;f.m_weight=num(s.get("font-weight"),400);return f;} float Font::measure(std::string_view v)const{float n=0;for(char c:v)n+=m_metrics.advance(c);return n;} }

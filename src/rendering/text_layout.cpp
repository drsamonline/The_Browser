#include "text_layout.hpp"
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
float TextLayout::measure_text(std::string_view text,float,StyleProperties const& style){ float width=0; auto font=Font::from_style(style); auto letter=parse_px(style.get("letter-spacing"),0); auto word=parse_px(style.get("word-spacing"),0); for(unsigned char c:text){ width+=font.metrics().advance(c); if(c==' ') width+=word; width+=letter;} return std::max(0.0f,width); }
std::vector<TextFragment> TextLayout::layout(std::string_view source,float x,float y,float available_width,StyleProperties const& style){
 std::vector<TextFragment> out; auto ws=style.get("white-space"); auto pre=ws&&(*ws=="pre"||*ws=="pre-wrap"); auto nowrap=ws&&(*ws=="nowrap"||*ws=="pre"); auto prewrap=ws&&*ws=="pre-wrap"; auto text=collapse_whitespace(source,pre); if(text.empty()||available_width<=0) return out; auto size=font_size(style), h=line_height(style); std::vector<std::vector<std::string>> lines(1); std::string token; auto flush=[&]{if(!token.empty()){lines.back().push_back(token);token.clear();}};
 for(char c:text){ if(c=='\n'&&pre){flush();lines.emplace_back();continue;} if(std::isspace((unsigned char)c)&&!pre){flush();continue;} if(std::isspace((unsigned char)c)&&pre){flush();lines.back().push_back(" ");continue;} token+=c;} flush();
 std::vector<std::vector<std::string>> wrapped; for(auto const& raw:lines){std::vector<std::string> line; float w=0; for(auto const& word:raw){float ww=measure_text(word,size,style); float sp=line.empty()?0:measure_text(" ",size,style); if(!nowrap&&!prewrap&&!line.empty()&&w+sp+ww>available_width){wrapped.push_back(line);line.clear();w=0;sp=0;} if(!line.empty()&&word!=" "){line.push_back(" ");w+=sp;} line.push_back(word);w+=ww;} wrapped.push_back(line);} auto align=style.get("text-align"); float cy=y; for(auto const& line:wrapped){float width=0;for(auto const& t:line) width+=measure_text(t,size,style); float cx=x; if(align&&*align=="center") cx=x+std::max(0.f,(available_width-width)/2); else if(align&&(*align=="right"||*align=="end")) cx=x+std::max(0.f,available_width-width); for(auto const& t:line){auto w=measure_text(t,size,style); if(t!=" ") out.push_back({t,{cx,cy,w,h},cy+size}); cx+=w;} cy+=h;} return out; }
} // namespace aetheris::rendering

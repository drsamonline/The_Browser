#include "image.hpp"
#include <cctype>
#include <sstream>
namespace aetheris::rendering {
Color Image::pixel(int x,int y) const { if(x<0||y<0||x>=width||y>=height)return {}; return pixels[size_t(y)*width+x]; }
static bool token(std::string const&s,size_t& p,std::string& o){ while(p<s.size()&&(std::isspace((unsigned char)s[p])||s[p]=='#')){if(s[p]=='#')while(p<s.size()&&s[p]!='\n')++p;else ++p;} size_t b=p;while(p<s.size()&&!std::isspace((unsigned char)s[p]))++p;o=s.substr(b,p-b);return !o.empty();}
std::shared_ptr<Image const> Image::load_ppm(ResourceData const& d){ if(d.bytes.size()<3)return{}; std::string s(d.bytes.begin(),d.bytes.end());size_t p=0;std::string t; if(!token(s,p,t)||t!="P3")return{};std::string a,b,c;if(!token(s,p,a)||!token(s,p,b)||!token(s,p,c))return{};auto img=std::make_shared<Image>();img->width=std::stoi(a);img->height=std::stoi(b);int maxv=std::stoi(c);if(img->width<=0||img->height<=0||maxv<=0)return{};img->pixels.reserve(size_t(img->width)*img->height);for(int i=0;i<img->width*img->height;i++){std::string r,g,bl;if(!token(s,p,r)||!token(s,p,g)||!token(s,p,bl))return{};img->pixels.push_back({uint8_t(std::stoi(r)*255/maxv),uint8_t(std::stoi(g)*255/maxv),uint8_t(std::stoi(bl)*255/maxv),255});}return img;}
std::shared_ptr<Image const> ImageCache::load(std::string const& u,ResourceCache const& r){auto it=m_images.find(u);if(it!=m_images.end())return it->second;auto d=r.get(u);if(!d||d->type!=ResourceType::Image)return{};auto i=Image::load_ppm(*d);if(i)m_images[u]=i;return i;} void ImageCache::clear(){m_images.clear();}
}

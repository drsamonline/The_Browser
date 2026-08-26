#include "cookie_jar.hpp"
#include <algorithm>
#include <sstream>
namespace aetheris::rendering {
std::string CookieJar::origin_key(Url const& u){ return u.scheme()+"://"+u.authority(); }
bool CookieJar::set(Url const& u,Cookie c){ if(!u.is_valid()||c.name.empty()) return false; if(c.domain.empty()) c.domain=u.authority(); auto& v=m_cookies[origin_key(u)]; auto it=std::find_if(v.begin(),v.end(),[&](auto const& x){return x.name==c.name&&x.path==c.path;}); if(it==v.end()) v.push_back(std::move(c)); else *it=std::move(c); return true; }
bool CookieJar::set_from_header(Url const& u,std::string const& h){ auto semi=h.find(';'); auto pair=h.substr(0,semi); auto eq=pair.find('='); if(eq==std::string::npos||eq==0) return false; Cookie c; c.name=pair.substr(0,eq); c.value=pair.substr(eq+1); c.domain=u.authority(); std::size_t p=semi==std::string::npos?std::string::npos:semi+1; while(p!=std::string::npos&&p<h.size()){ auto n=h.find(';',p); auto part=h.substr(p,n==std::string::npos?std::string::npos:n-p); auto e=part.find('='); auto key=part.substr(0,e); auto value=e==std::string::npos?"":part.substr(e+1); while(!key.empty()&&key.front()==' ') key.erase(key.begin()); if(key=="Path") c.path=value; if(key=="Secure") c.secure=true; p=n==std::string::npos?n:n+1; } return set(u,std::move(c)); }
std::vector<Cookie> CookieJar::cookies_for(Url const& u) const{ std::vector<Cookie> out; auto it=m_cookies.find(origin_key(u)); if(it==m_cookies.end()) return out; for(auto const& c:it->second) if((!c.secure||u.scheme()=="https")&&u.path().starts_with(c.path)) out.push_back(c); return out; }
std::string CookieJar::cookie_header(Url const& u) const{ std::string out; for(auto const& c:cookies_for(u)){ if(!out.empty()) out+="; "; out+=c.name+"="+c.value; } return out; }
void CookieJar::clear_origin(Url const& u){m_cookies.erase(origin_key(u));}
}

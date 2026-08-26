#pragma once
#include "url.hpp"
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
namespace aetheris::rendering {
struct Cookie { std::string name; std::string value; std::string domain; std::string path { "/" }; bool secure { false }; };
class CookieJar {
public:
    bool set(Url const&, Cookie);
    bool set_from_header(Url const&, std::string const&);
    std::vector<Cookie> cookies_for(Url const&) const;
    std::string cookie_header(Url const&) const;
    void clear_origin(Url const&);
private:
    static std::string origin_key(Url const&);
    std::unordered_map<std::string, std::vector<Cookie>> m_cookies;
};
}

#pragma once
#include "cookie_jar.hpp"
#include "resource_backend.hpp"
#include <functional>
#include <map>
namespace aetheris::rendering {
struct HttpRequest { Url url; std::string method { "GET" }; std::map<std::string,std::string> headers; std::vector<unsigned char> body; };
struct HttpResponse { int status { 0 }; Url final_url; std::map<std::string,std::string> headers; std::vector<unsigned char> body; std::string content_type; std::string message; bool succeeded() const { return status>=200&&status<400; } };
class HttpResourceBackend final: public ResourceBackend {
public:
    using Transport=std::function<HttpResponse(HttpRequest const&)>;
    HttpResourceBackend(Transport, CookieJar* = nullptr);
    bool supports(Url const&) const override;
    ResourceBackendResult load(Url const&) override;
private: Transport m_transport; CookieJar* m_cookies { nullptr }; static ResourceType infer_type(std::string const&, Url const&); };
}

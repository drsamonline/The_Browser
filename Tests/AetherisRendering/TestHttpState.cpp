#include "cookie_jar.hpp"
#include "http_resource_backend.hpp"
#include <cassert>
using namespace aetheris::rendering;
int main(){CookieJar jar;auto url=Url::parse("https://example.test/index.html");HttpResourceBackend backend([](HttpRequest const& r){HttpResponse x;x.status=200;x.final_url=r.url;x.content_type="text/html";x.headers["Set-Cookie"]="sid=abc; Path=/; Secure";x.body={'<','h','1','>','O','K','<','/','h','1','>'};return x;},&jar);assert(backend.supports(url));auto result=backend.load(url);assert(result.succeeded());assert(result.resource->type==ResourceType::Document);assert(jar.cookie_header(url)=="sid=abc");auto css=Url::parse("http://example.test/site.css");assert(HttpResourceBackend([](HttpRequest const& r){HttpResponse x;x.status=200;x.final_url=r.url;x.content_type="text/css";return x;}).load(css).succeeded());}

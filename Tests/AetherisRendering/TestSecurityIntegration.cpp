#include "security_policy.hpp"
#include <cassert>
using namespace aetheris::rendering;
int main()
{
    auto secure_doc = Url::parse("https://a.test/index.html");
    auto secure_style = Url::parse("https://cdn.test/site.css");
    auto insecure_style = Url::parse("http://cdn.test/site.css");
    auto local = Url::parse("file:///tmp/page.html");
    auto remote = Url::parse("https://a.test/x");
    assert(SecurityPolicy::allows_resource(secure_doc, secure_style, ResourceRequestContext::Style));
    assert(!SecurityPolicy::allows_resource(secure_doc, insecure_style, ResourceRequestContext::Style));
    assert(!SecurityPolicy::allows_resource(local, remote, ResourceRequestContext::Subresource));
}

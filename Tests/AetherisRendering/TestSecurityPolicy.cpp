// Mixed content (row 5c) and the trustworthiness question underneath it. The assertions are plain `require` rather
// than `assert`, because `assert` disappears under NDEBUG and a security test that vanishes is worse than none.
#include "security_policy.hpp"
#include "secure_resource_loader.hpp"
#include "resource_loader.hpp"
#include "resource_backend.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>

using namespace aetheris::rendering;

static void require(bool condition, char const* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

class DummyBackend final : public ResourceBackend {
public:
    bool supports(Url const&) const override { return true; }
    ResourceBackendResult load(Url const& url) override
    {
        ResourceData data;
        data.url = url.serialized();
        data.type = ResourceType::Image;
        data.bytes = { 'x' };
        return { ResourceBackendState::Ready, std::move(data), {} };
    }
};

int main()
{
    auto const https = Url::parse("https://Example.test/a");
    auto const https_same = Url::parse("https://example.test/b");
    auto const http = Url::parse("http://example.test/x");
    auto const script = Url::parse("http://example.test/app.js");

    // Origins.
    require(SecurityPolicy::is_same_origin(https, https_same), "the same origin, whatever the case of the host");
    require(!SecurityPolicy::is_same_origin(https, http), "a different scheme is a different origin");

    // Trustworthiness, the question mixed content is really asking. Upstream asks it of an origin
    // (LibWeb/SecureContexts/AbstractOperations.cpp:15-57) and treats https, wss, file and loopback as trustworthy.
    require(SecurityPolicy::is_potentially_trustworthy(https), "https is trustworthy");
    require(SecurityPolicy::is_potentially_trustworthy(Url::parse("file:///tmp/x.html")), "file is trustworthy");
    require(SecurityPolicy::is_potentially_trustworthy(Url::parse("http://localhost:8080/")), "localhost is trustworthy");
    require(SecurityPolicy::is_potentially_trustworthy(Url::parse("http://127.0.0.1/")), "127.0.0.1 is trustworthy");
    require(SecurityPolicy::is_potentially_trustworthy(Url::parse("http://127.5.6.7/")), "the whole of 127/8 is trustworthy");
    require(SecurityPolicy::is_potentially_trustworthy(Url::parse("http://a.localhost/")), "*.localhost is trustworthy");
    require(!SecurityPolicy::is_potentially_trustworthy(http), "a plain http host is not");
    require(!SecurityPolicy::is_potentially_trustworthy(Url::parse("http://127.0.0.1.evil.test/")),
        "a host that starts with a loopback address but is not one is not trustworthy");
    require(!SecurityPolicy::is_potentially_trustworthy(Url::parse("http://notlocalhost.test/")),
        "a host that merely ends in the word is not localhost");

    // Mixed content: a secure document may not fetch insecure blockable content.
    require(SecurityPolicy::allows_resource(https, http, ResourceRequestContext::Image), "an image is optionally blockable");
    require(!SecurityPolicy::allows_resource(https, script, ResourceRequestContext::Script), "a script is not");
    require(!SecurityPolicy::allows_resource(https, Url::parse("http://example.test/a.css"), ResourceRequestContext::Style),
        "nor is a stylesheet");
    require(!SecurityPolicy::allows_resource(https, http, ResourceRequestContext::Document), "nor a document");
    require(!SecurityPolicy::allows_resource(https, http, ResourceRequestContext::FormSubmission), "nor a form submission");
    require(!SecurityPolicy::allows_resource(https, Url::parse("data:text/plain,x"), ResourceRequestContext::Subresource),
        "an unsupported scheme is refused outright");

    // An insecure document has no mixed content, and a loopback resource is not insecure - both were blocked before.
    require(SecurityPolicy::allows_resource(http, script, ResourceRequestContext::Script),
        "http over http is not mixed content");
    require(SecurityPolicy::allows_resource(https, Url::parse("http://localhost:3000/app.js"), ResourceRequestContext::Script),
        "a loopback script is not mixed content");
    require(SecurityPolicy::allows_resource(https, Url::parse("https://example.test/app.js"), ResourceRequestContext::Script),
        "and neither is a secure one");

    // The loader is where the policy is enforced, not merely consulted.
    ResourceCache cache;
    ResourceLoader loader(cache, std::vector<std::shared_ptr<ResourceBackend>> { std::make_shared<DummyBackend>() });
    SecureResourceLoader secure(loader, https);
    require(secure.load(ResourceRequestContext::Image, http).succeeded(), "the loader lets an image through");
    require(!secure.load(ResourceRequestContext::Script, script).succeeded(), "and refuses a script");

    std::cout << "security policy: mixed content and trustworthiness verified\n";
    return 0;
}

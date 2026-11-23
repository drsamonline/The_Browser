// Mixed content (row 5c) and the trustworthiness question underneath it. The assertions are plain `require` rather
// than `assert`, because `assert` disappears under NDEBUG and a security test that vanishes is worse than none.
#include "content_security_policy.hpp"
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

// Row 5a: the fetch directives. Upstream asks, per request, for the effective directive and then whether the
    // request matches that directive's source list (ContentSecurityPolicy/Directives/ImageSourceDirective.cpp:27-43).
    // These are the same two questions.
    {
        auto const page = Url::parse("https://example.test/page.html");
        auto const cdn = Url::parse("https://cdn.example.test/logo.png");
        auto const other = Url::parse("https://evil.test/logo.png");
        auto const plain = Url::parse("http://example.test/logo.png");

        auto const empty = ContentSecurityPolicy::parse("");
        require(empty.empty(), "an empty policy is empty");
        require(empty.allows_fetch("img-src", other, page), "no policy allows everything");
        require(empty.allows_inline_script(), "and inline scripts run");

        auto const images = ContentSecurityPolicy::parse("default-src 'self'; img-src 'self' https://cdn.example.test");
        require(images.allows_fetch("img-src", page, page), "'self' matches the document's own origin");
        require(images.allows_fetch("img-src", cdn, page), "an explicit host source matches");
        require(!images.allows_fetch("img-src", other, page), "another host does not");
        require(!images.allows_fetch("img-src", plain, page), "nor does another scheme on the same host");

        // default-src stands in for a directive that is absent, and never for one that is present.
        require(!images.allows_fetch("connect-src", other, page), "connect-src falls back to default-src 'self'");
        require(images.allows_fetch("connect-src", page, page), "which allows the origin itself");

        auto const wildcard = ContentSecurityPolicy::parse("img-src *");
        require(wildcard.allows_fetch("img-src", other, page), "* allows any host");

        auto const none = ContentSecurityPolicy::parse("img-src 'none'");
        require(!none.allows_fetch("img-src", page, page), "'none' refuses even the origin");

        auto const subdomains = ContentSecurityPolicy::parse("img-src *.example.test");
        require(subdomains.allows_fetch("img-src", cdn, page), "*.example.test matches a subdomain");
        require(!subdomains.allows_fetch("img-src", page, page), "but not the bare domain");

        auto const by_scheme = ContentSecurityPolicy::parse("img-src https:");
        require(by_scheme.allows_fetch("img-src", other, page), "a scheme source matches any host on that scheme");
        require(!by_scheme.allows_fetch("img-src", plain, page), "and not another scheme");

        // Inline: 'unsafe-inline' or nothing restricting it.
        auto const strict = ContentSecurityPolicy::parse("script-src 'self'; style-src 'self'");
        require(!strict.allows_inline_script(), "a script-src without 'unsafe-inline' blocks inline scripts");
        require(!strict.allows_inline_style(), "and style-src does the same for styles");
        auto const loose = ContentSecurityPolicy::parse("script-src 'unsafe-inline'");
        require(loose.allows_inline_script(), "'unsafe-inline' allows them");
        require(loose.allows_inline_style(), "and with no style-src, styles fall back to nothing restricting them");

        // The loader enforces it, on the directive that matches the request's context.
        ResourceCache policy_cache;
        ResourceLoader policy_loader(policy_cache,
            std::vector<std::shared_ptr<ResourceBackend>> { std::make_shared<DummyBackend>() });
        SecureResourceLoader policy_secure(policy_loader, page);
        policy_secure.set_content_security_policy(
            ContentSecurityPolicy::parse("img-src 'self'; script-src https://cdn.example.test"));
        require(policy_secure.load(ResourceRequestContext::Image, page).succeeded(), "the loader allows a matching image");
        require(!policy_secure.load(ResourceRequestContext::Image, other).succeeded(), "and refuses a foreign one");
        require(policy_secure.load(ResourceRequestContext::Script, cdn).succeeded(), "a script from the allowed host runs");
        require(!policy_secure.load(ResourceRequestContext::Script, page).succeeded(),
            "while one from the document's own host does not, because script-src did not name it");
    }

    std::cout << "security policy: mixed content, trustworthiness and CSP fetch directives verified\n";
    return 0;
}

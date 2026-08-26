#include "security_policy.hpp"
#include "secure_resource_loader.hpp"
#include "resource_loader.hpp"
#include "resource_backend.hpp"
#include <cassert>
#include <memory>
using namespace aetheris::rendering;

class DummyBackend final : public ResourceBackend {
public:
    bool supports(Url const&) const override { return true; }
    ResourceBackendResult load(Url const& url) override {
        ResourceData data; data.url = url.serialized(); data.type = ResourceType::Image; data.bytes = { 'x' };
        return { ResourceBackendState::Ready, std::move(data), {} };
    }
};

int main()
{
    auto https = Url::parse("https://Example.test/a");
    auto https_same = Url::parse("https://example.test/b");
    auto http = Url::parse("http://example.test/x");
    auto script = Url::parse("http://example.test/app.js");
    assert(SecurityPolicy::is_same_origin(https, https_same));
    assert(!SecurityPolicy::is_same_origin(https, http));
    assert(SecurityPolicy::allows_resource(https, http, ResourceRequestContext::Image));
    assert(!SecurityPolicy::allows_resource(https, script, ResourceRequestContext::Script));
    assert(!SecurityPolicy::allows_resource(https, Url::parse("data:text/plain,x"), ResourceRequestContext::Subresource));
    ResourceCache cache;
    ResourceLoader loader(cache, std::vector<std::shared_ptr<ResourceBackend>> { std::make_shared<DummyBackend>() });
    SecureResourceLoader secure(loader, https);
    assert(secure.load(ResourceRequestContext::Image, http).succeeded());
    assert(!secure.load(ResourceRequestContext::Script, script).succeeded());
}

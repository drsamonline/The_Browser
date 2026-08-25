#include "resource_loader.hpp"
#include <cassert>

using namespace aetheris::rendering;

int main()
{
    ResourceCache cache;
    Url document_url("https://example.test/index.html");
    ResourceLoader loader(cache, [](Url const& url) -> std::optional<ResourceData> {
        if (url.serialized() == "https://example.test/index.html")
            return ResourceData { ResourceType::Document, url.serialized(), std::vector<unsigned char>{ '<','l','i','n','k',' ','h','r','e','f','=','\"','s','t','y','l','e','.','c','s','s','\"','>','<','i','m','g',' ','s','r','c','=','\"','i','m','g','.','p','n','g','\"','>' } };
        if (url.serialized() == "https://example.test/style.css")
            return ResourceData { ResourceType::Stylesheet, url.serialized(), {} };
        return std::nullopt;
    });
    auto document = loader.load(ResourceType::Document, document_url);
    assert(document.succeeded());
    assert(cache.contains(document_url.serialized()));
    auto dependencies = loader.discover_dependencies(*document.resource, document_url);
    assert(dependencies.size() == 2);
    assert(dependencies[0].serialized() == "https://example.test/style.css");
    assert(dependencies[1].serialized() == "https://example.test/img.png");
    auto stylesheet = loader.load(ResourceType::Stylesheet, dependencies[0]);
    assert(stylesheet.succeeded());
    auto missing = loader.load(dependencies[1]);
    assert(!missing.succeeded());
    return 0;
}

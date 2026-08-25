#include "resource_loader.hpp"

#include <regex>

namespace aetheris::rendering {

ResourceLoader::ResourceLoader(ResourceCache& cache, Fetcher fetcher)
    : m_cache(cache)
    , m_fetcher(std::move(fetcher))
{
}

ResourceLoader::ResourceLoader(ResourceCache& cache, std::vector<std::shared_ptr<ResourceBackend>> backends)
    : m_cache(cache)
    , m_backends(std::move(backends))
{
}

void ResourceLoader::add_backend(std::shared_ptr<ResourceBackend> backend)
{
    if (backend)
        m_backends.push_back(std::move(backend));
}

ResourceLoadResult ResourceLoader::load(Url const& url)
{
    if (!url.is_valid())
        return { ResourceLoadState::Failed, nullptr, "Invalid resource URL" };
    if (auto cached = m_cache.get(url.serialized()))
        return { ResourceLoadState::Ready, std::move(cached), {} };
    if (m_fetcher) {
        auto fetched = m_fetcher(url);
        if (fetched) {
            if (fetched->url.empty())
                fetched->url = url.serialized();
            if (fetched->url != url.serialized())
                return { ResourceLoadState::Failed, nullptr, "Loaded resource URL does not match request" };
            m_cache.put(std::move(*fetched));
            return { ResourceLoadState::Ready, m_cache.get(url.serialized()), {} };
        }
    }

    bool supported = false;
    std::string failure;
    for (auto const& backend : m_backends) {
        if (!backend || !backend->supports(url))
            continue;
        supported = true;
        auto result = backend->load(url);
        if (result.succeeded()) {
            auto fetched = std::move(*result.resource);
            if (fetched.url.empty())
                fetched.url = url.serialized();
            if (fetched.url != url.serialized())
                return { ResourceLoadState::Failed, nullptr, "Loaded resource URL does not match request" };
            m_cache.put(std::move(fetched));
            return { ResourceLoadState::Ready, m_cache.get(url.serialized()), {} };
        }
        if (!result.message.empty())
            failure = std::move(result.message);
    }
    if (!supported)
        return { ResourceLoadState::Missing, nullptr, "Resource is not cached and no backend supports the URL" };
    return { ResourceLoadState::Missing, nullptr, failure.empty() ? "Resource backend did not provide the requested resource" : failure };
}

ResourceLoadResult ResourceLoader::load(ResourceType expected_type, Url const& url)
{
    auto result = load(url);
    if (!result.succeeded())
        return result;
    if (result.resource->type != expected_type)
        return { ResourceLoadState::Failed, nullptr, "Loaded resource has an unexpected type" };
    return result;
}

std::vector<Url> ResourceLoader::discover_dependencies(ResourceData const& document, Url const& base_url) const
{
    std::vector<Url> urls;
    if (document.type != ResourceType::Document)
        return urls;
    auto text = std::string(document.bytes.begin(), document.bytes.end());
    std::regex pattern(R"((?:href|src)\s*=\s*[\"']([^\"']+)[\"'])", std::regex::icase);
    for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) {
        auto raw = (*it)[1].str();
        auto resolved = Url::resolve(base_url, raw);
        if (resolved.is_valid())
            urls.push_back(std::move(resolved));
    }
    return urls;
}

} // namespace aetheris::rendering

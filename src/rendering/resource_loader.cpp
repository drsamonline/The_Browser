#include "resource_loader.hpp"

#include <regex>

namespace aetheris::rendering {

ResourceLoader::ResourceLoader(ResourceCache& cache, Fetcher fetcher)
    : m_cache(cache)
    , m_fetcher(std::move(fetcher))
{
}

ResourceLoadResult ResourceLoader::load(Url const& url)
{
    if (!url.is_valid())
        return { ResourceLoadState::Failed, nullptr, "Invalid resource URL" };
    if (auto cached = m_cache.get(url.serialized()))
        return { ResourceLoadState::Ready, std::move(cached), {} };
    if (!m_fetcher)
        return { ResourceLoadState::Missing, nullptr, "Resource is not cached and no loader is available" };

    auto fetched = m_fetcher(url);
    if (!fetched)
        return { ResourceLoadState::Missing, nullptr, "Resource loader did not provide the requested resource" };
    if (fetched->url.empty())
        fetched->url = url.serialized();
    if (fetched->url != url.serialized())
        return { ResourceLoadState::Failed, nullptr, "Loaded resource URL does not match request" };
    m_cache.put(std::move(*fetched));
    return { ResourceLoadState::Ready, m_cache.get(url.serialized()), {} };
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

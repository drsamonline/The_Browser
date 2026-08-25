#include "resource.hpp"

namespace aetheris::rendering {

void ResourceCache::put(ResourceData data)
{
    auto resource = std::make_shared<ResourceData>(std::move(data));
    m_resources[resource->url] = std::move(resource);
}

void ResourceCache::put_text(ResourceType type, std::string url, std::string_view text)
{
    ResourceData data { type, std::move(url), {} };
    data.bytes.assign(text.begin(), text.end());
    put(std::move(data));
}

std::shared_ptr<ResourceData const> ResourceCache::get(std::string const& url) const
{
    auto it = m_resources.find(url);
    return it == m_resources.end() ? nullptr : it->second;
}

std::string ResourceCache::get_text(std::string const& url) const
{
    auto resource = get(url);
    if (!resource)
        return {};
    return { resource->bytes.begin(), resource->bytes.end() };
}

bool ResourceCache::contains(std::string const& url) const
{
    return m_resources.contains(url);
}

void ResourceCache::clear()
{
    m_resources.clear();
}

} // namespace aetheris::rendering

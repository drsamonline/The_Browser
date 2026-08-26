#include "secure_resource_loader.hpp"

namespace aetheris::rendering {

SecureResourceLoader::SecureResourceLoader(ResourceLoader& loader, Url document_url)
    : m_loader(loader), m_document_url(std::move(document_url))
{
}

ResourceLoadResult SecureResourceLoader::load(ResourceRequestContext context, Url const& url)
{
    std::string reason;
    if (!SecurityPolicy::allows_resource(m_document_url, url, context, &reason))
        return { ResourceLoadState::Failed, nullptr, std::move(reason) };
    return m_loader.load(url);
}

} // namespace aetheris::rendering

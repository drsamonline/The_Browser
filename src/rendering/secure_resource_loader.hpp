#pragma once

#include "resource_loader.hpp"
#include "security_policy.hpp"

namespace aetheris::rendering {

class SecureResourceLoader {
public:
    SecureResourceLoader(ResourceLoader&, Url document_url);
    ResourceLoadResult load(ResourceRequestContext, Url const&);
    Url const& document_url() const { return m_document_url; }

private:
    ResourceLoader& m_loader;
    Url m_document_url;
};

} // namespace aetheris::rendering

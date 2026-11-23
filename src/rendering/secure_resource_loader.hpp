#pragma once

#include "resource_loader.hpp"
#include "content_security_policy.hpp"
#include "security_policy.hpp"

namespace aetheris::rendering {

class SecureResourceLoader {
public:
    SecureResourceLoader(ResourceLoader&, Url document_url);
    ResourceLoadResult load(ResourceRequestContext, Url const&);

    // The document's Content-Security-Policy. Set before the first load; a loader without one enforces only the
    // scheme and mixed-content rules (row 5a).
    void set_content_security_policy(ContentSecurityPolicy policy) { m_policy = std::move(policy); }
    ContentSecurityPolicy const& content_security_policy() const { return m_policy; }
    Url const& document_url() const { return m_document_url; }

private:
    ResourceLoader& m_loader;
    Url m_document_url;
    ContentSecurityPolicy m_policy;
};

} // namespace aetheris::rendering

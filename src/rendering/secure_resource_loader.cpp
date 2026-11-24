#include "secure_resource_loader.hpp"

namespace aetheris::rendering {

SecureResourceLoader::SecureResourceLoader(ResourceLoader& loader, Url document_url)
    : m_loader(loader), m_document_url(std::move(document_url))
{
}

namespace {

// The fetch directive that governs a request. Upstream asks the same question in
// `get_the_effective_directive_for_request` (ContentSecurityPolicy/Directives/ImageSourceDirective.cpp:30).
std::string_view directive_for(ResourceRequestContext context)
{
    switch (context) {
    case ResourceRequestContext::Image:
        return "img-src";
    case ResourceRequestContext::Font:
        return "font-src";
    case ResourceRequestContext::Style:
        return "style-src";
    case ResourceRequestContext::Script:
        return "script-src";
    case ResourceRequestContext::Document:
        return "frame-src";
    case ResourceRequestContext::FormSubmission:
        return "form-action";
    case ResourceRequestContext::Subresource:
        break;
    }
    return "connect-src";
}

} // namespace

ResourceLoadResult SecureResourceLoader::load(ResourceRequestContext context, Url const& url)
{
    std::string reason;
    if (!SecurityPolicy::allows_resource(m_document_url, url, context, &reason))
        return { ResourceLoadState::Failed, nullptr, std::move(reason) };
    // The policy is checked after the scheme and mixed-content rules, so a request refused for two reasons reports
    // the more fundamental one.
    if (!m_policy.empty() && !m_policy.allows_fetch(directive_for(context), url, m_document_url))
        return { ResourceLoadState::Failed, nullptr, "Blocked by Content-Security-Policy" };
    return m_loader.load(url);
}

} // namespace aetheris::rendering

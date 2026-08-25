#include "navigation.hpp"

namespace aetheris::rendering {

NavigationResult NavigationController::navigate(NavigationRequest const& request, ResourceCache& resources)
{
    if (!request.document_url.is_valid())
        return { NavigationError::InvalidUrl, "Navigation requires a document URL" };
    if (request.viewport_width <= 0)
        return { NavigationError::DocumentCreationFailed, "Navigation requires a positive viewport width" };

    auto document = resources.get(request.document_url.serialized());
    if (!document)
        return { NavigationError::MissingDocument, "Document resource was not found" };
    if (document->type != ResourceType::Document)
        return { NavigationError::InvalidResourceType, "Navigation target is not a document resource" };

    std::string stylesheet;
    if (request.stylesheet_url) {
        auto resolved = request.stylesheet_url->is_absolute()
            ? *request.stylesheet_url
            : Url::resolve(request.document_url, request.stylesheet_url->serialized());

        auto style = resources.get(resolved.serialized());
        if (!style)
            return { NavigationError::MissingStylesheet, "Stylesheet resource was not found" };
        if (style->type != ResourceType::Stylesheet)
            return { NavigationError::InvalidResourceType, "Stylesheet target is not a stylesheet resource" };
        stylesheet = resolved.serialized();
    } else {
        return { NavigationError::MissingStylesheet, "Navigation currently requires an explicit stylesheet resource" };
    }

    auto rendered = RenderDocument::create_from_resources(
        request.document_url.serialized(), stylesheet, request.viewport_width, resources);
    if (!rendered)
        return { NavigationError::DocumentCreationFailed, "Unable to create render document from navigation resources" };

    m_current_document = std::move(*rendered);
    m_current_url = request.document_url;
    return {};
}

RenderDocument const* NavigationController::current_document() const
{
    return m_current_document ? &*m_current_document : nullptr;
}

} // namespace aetheris::rendering

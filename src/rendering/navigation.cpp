#include "navigation.hpp"
#include <algorithm>
#include <functional>
#include <vector>

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

namespace aetheris::rendering {

NavigationResult NavigationController::navigate(NavigationRequest const& request, ResourceLoader& loader)
{
    m_lifecycle = { PageLoadState::Loading, request.document_url, {} };
    if (!request.document_url.is_valid()) { m_lifecycle.state=PageLoadState::Failed; m_lifecycle.message="Navigation requires a document URL"; return { NavigationError::InvalidUrl, m_lifecycle.message }; }
    if (request.viewport_width <= 0) { m_lifecycle.state=PageLoadState::Failed; m_lifecycle.message="Navigation requires a positive viewport width"; return { NavigationError::DocumentCreationFailed, m_lifecycle.message }; }
    auto document = loader.load(ResourceType::Document, request.document_url);
    if (!document.succeeded()) { m_lifecycle.state=PageLoadState::Failed; m_lifecycle.message=document.message; return { NavigationError::MissingDocument, document.message }; }

    std::vector<Url> styles;
    if (request.stylesheet_url) styles.push_back(request.stylesheet_url->is_absolute() ? *request.stylesheet_url : Url::resolve(request.document_url, request.stylesheet_url->serialized()));
    else {
        Document parsed = Document::parse_html(loader.cache().get_text(request.document_url.serialized()));
        std::function<void(DomNode const&)> walk = [&](DomNode const& node) {
            if (node.type == DomNodeType::Element && node.name == "link") {
                auto rel=node.attribute("rel"), href=node.attribute("href");
                if (rel && href && *rel == "stylesheet") styles.push_back(Url::resolve(request.document_url,*href));
            }
            for (auto const& child : node.children) walk(*child);
        };
        walk(parsed.root());
    }
    std::vector<std::string> stylesheet_urls;
    for (auto const& style_url : styles) {
        if (!style_url.is_valid()) continue;
        if (std::find(stylesheet_urls.begin(), stylesheet_urls.end(), style_url.serialized()) != stylesheet_urls.end()) continue;
        auto stylesheet = loader.load(ResourceType::Stylesheet, style_url);
        if (!stylesheet.succeeded()) { m_lifecycle.state=PageLoadState::Failed; m_lifecycle.message=stylesheet.message; return { NavigationError::MissingStylesheet, stylesheet.message }; }
        stylesheet_urls.push_back(style_url.serialized());
    }
    if (stylesheet_urls.empty()) { m_lifecycle.state=PageLoadState::Failed; m_lifecycle.message="No stylesheet resources were discovered"; return { NavigationError::MissingStylesheet, m_lifecycle.message }; }

    Document parsed = Document::parse_html(loader.cache().get_text(request.document_url.serialized()));
    std::function<ResourceLoadResult(DomNode const&)> load_images = [&](DomNode const& node) -> ResourceLoadResult {
        if (node.type == DomNodeType::Element && node.name == "img") if (auto src=node.attribute("src")) { auto result=loader.load(ResourceType::Image, Url::resolve(request.document_url,*src)); if (!result.succeeded()) return result; }
        for (auto const& child : node.children) { auto result=load_images(*child); if (!result.succeeded()) return result; }
        return { ResourceLoadState::Ready, document.resource, {} };
    };
    auto images=load_images(parsed.root());
    if (!images.succeeded()) { m_lifecycle.state=PageLoadState::Failed; m_lifecycle.message=images.message; return { NavigationError::DocumentCreationFailed, images.message }; }

    auto rendered = RenderDocument::create_from_resources(request.document_url.serialized(), stylesheet_urls, request.viewport_width, loader.cache());
    if (!rendered) { m_lifecycle.state=PageLoadState::Failed; m_lifecycle.message="Unable to create render document from navigation resources"; return { NavigationError::DocumentCreationFailed, m_lifecycle.message }; }
    m_current_document = std::move(*rendered); m_current_url = request.document_url; m_last_request=request; m_lifecycle={PageLoadState::Complete,m_current_url,{}}; return {};
}

NavigationResult NavigationController::reload(ResourceLoader& loader)
{
    if (!m_last_request) return { NavigationError::InvalidUrl, "No successful navigation is available to reload" };
    return navigate(*m_last_request, loader);
}

} // namespace aetheris::rendering

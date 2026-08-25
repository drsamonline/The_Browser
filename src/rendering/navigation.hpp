#pragma once

#include "render_document.hpp"
#include "page_lifecycle.hpp"
#include "resource.hpp"
#include "resource_loader.hpp"
#include "url.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace aetheris::rendering {

enum class NavigationError {
    None,
    InvalidUrl,
    MissingDocument,
    MissingStylesheet,
    InvalidResourceType,
    DocumentCreationFailed,
};

struct NavigationRequest {
    Url document_url;
    std::optional<Url> stylesheet_url;
    float viewport_width { 0 };
};

struct NavigationResult {
    NavigationError error { NavigationError::None };
    std::string message;

    bool succeeded() const { return error == NavigationError::None; }
};

class NavigationController {
public:
    NavigationResult navigate(NavigationRequest const&, ResourceCache&);
    NavigationResult navigate(NavigationRequest const&, ResourceLoader&);

    RenderDocument const* current_document() const;
    Url const& current_url() const { return m_current_url; }
    PageLifecycleState const& lifecycle() const { return m_lifecycle; }
    NavigationResult reload(ResourceLoader&);
    bool has_document() const { return m_current_document.has_value(); }

private:
    std::optional<RenderDocument> m_current_document;
    Url m_current_url;
    PageLifecycleState m_lifecycle;
    std::optional<NavigationRequest> m_last_request;
};

} // namespace aetheris::rendering

#pragma once

#include "url.hpp"
#include <string>
#include <string_view>

namespace aetheris::rendering {

enum class ResourceRequestContext { Document, Subresource, Script, Style, Image, FormSubmission };

class SecurityPolicy {
public:
    static std::string origin(Url const&);
    static bool is_same_origin(Url const&, Url const&);
    static bool is_secure_scheme(Url const&);
    static bool is_supported_scheme(Url const&);
    static bool allows_mixed_content(Url const& document_url, Url const& resource_url, ResourceRequestContext);
    static bool allows_resource(Url const& document_url, Url const& resource_url, ResourceRequestContext, std::string* reason = nullptr);
};

} // namespace aetheris::rendering

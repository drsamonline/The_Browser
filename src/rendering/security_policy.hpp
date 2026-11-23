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
    // Whether a URL is potentially trustworthy: https, wss, file, or a loopback host. Upstream asks the same
    // question of an origin (LibWeb/SecureContexts/AbstractOperations.cpp:15-57) and uses the answer to decide
    // whether a document is secure at all - an http://localhost page is a secure context, which is why mixed
    // content is not a thing on it (row 5c).
    static bool is_potentially_trustworthy(Url const&);
    // "localhost", "*.localhost", 127.0.0.0/8 and ::1 - the hosts a browser lets be insecure.
    static bool is_loopback_host(std::string const& host);
    static bool is_supported_scheme(Url const&);
    static bool allows_mixed_content(Url const& document_url, Url const& resource_url, ResourceRequestContext);
    static bool allows_resource(Url const& document_url, Url const& resource_url, ResourceRequestContext, std::string* reason = nullptr);
};

} // namespace aetheris::rendering

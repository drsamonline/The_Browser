#include "security_policy.hpp"
#include <algorithm>
#include <cctype>

namespace aetheris::rendering {

static std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string SecurityPolicy::origin(Url const& url)
{
    if (!url.is_absolute() || url.scheme().empty() || url.authority().empty())
        return {};
    return lower(url.scheme()) + "://" + lower(url.authority());
}

bool SecurityPolicy::is_same_origin(Url const& a, Url const& b)
{
    auto a_origin = origin(a);
    auto b_origin = origin(b);
    return !a_origin.empty() && a_origin == b_origin;
}

bool SecurityPolicy::is_secure_scheme(Url const& url)
{
    return lower(url.scheme()) == "https";
}

bool SecurityPolicy::is_supported_scheme(Url const& url)
{
    auto scheme = lower(url.scheme());
    return scheme == "http" || scheme == "https" || scheme == "file";
}

bool SecurityPolicy::allows_mixed_content(Url const& document_url, Url const& resource_url, ResourceRequestContext context)
{
    if (!is_secure_scheme(document_url) || lower(resource_url.scheme()) != "http")
        return true;
    return context == ResourceRequestContext::Image;
}

bool SecurityPolicy::allows_resource(Url const& document_url, Url const& resource_url, ResourceRequestContext context, std::string* reason)
{
    if (!resource_url.is_valid()) {
        if (reason) *reason = "Invalid resource URL";
        return false;
    }
    if (!is_supported_scheme(resource_url)) {
        if (reason) *reason = "Unsupported resource scheme";
        return false;
    }
    if (!allows_mixed_content(document_url, resource_url, context)) {
        if (reason) *reason = "Blocked insecure active content from a secure document";
        return false;
    }
    if (lower(document_url.scheme()) == "file" && resource_url.is_absolute() && lower(resource_url.scheme()) != "file") {
        if (reason) *reason = "Blocked network resource from file document";
        return false;
    }
    return true;
}

} // namespace aetheris::rendering

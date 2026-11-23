#include "security_policy.hpp"
#include <algorithm>
#include <cctype>
#include <vector>

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

bool SecurityPolicy::is_loopback_host(std::string const& host)
{
    auto const name = lower(host);
    if (name == "localhost" || name == "localhost." || name == "::1" || name == "[::1]")
        return true;
    if (name.size() > 10 && name.compare(name.size() - 10, 10, ".localhost") == 0)
        return true;

    // 127.0.0.0/8. Four numeric labels and nothing else: "127.0.0.1.evil.test" starts with the right digits and is
    // not loopback, which is the case this function exists to get right.
    std::vector<std::string> labels;
    std::string label;
    for (char c : name) {
        if (c == '.') {
            labels.push_back(label);
            label.clear();
        } else {
            label += c;
        }
    }
    labels.push_back(label);
    if (labels.size() != 4 || labels[0] != "127")
        return false;
    for (auto const& part : labels) {
        if (part.empty() || part.size() > 3)
            return false;
        if (!std::all_of(part.begin(), part.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
            return false;
        if (std::stoi(part) > 255)
            return false;
    }
    return true;
}

bool SecurityPolicy::is_potentially_trustworthy(Url const& url)
{
    auto const scheme = lower(url.scheme());
    if (scheme == "https" || scheme == "wss" || scheme == "file")
        return true;
    if (scheme != "http")
        return false;
    // The authority is "host:port"; the port says nothing about trust.
    auto const& authority = url.authority();
    auto const colon = authority.rfind(':');
    auto const host = colon == std::string::npos ? authority : authority.substr(0, colon);
    return is_loopback_host(host);
}

bool SecurityPolicy::is_supported_scheme(Url const& url)
{
    auto scheme = lower(url.scheme());
    return scheme == "http" || scheme == "https" || scheme == "file";
}

bool SecurityPolicy::allows_mixed_content(Url const& document_url, Url const& resource_url, ResourceRequestContext context)
{
    // Mixed content is a question about a secure document fetching an insecure subresource. A document that is not
    // secure has no mixed content to speak of, and a resource that is itself potentially trustworthy is not
    // insecure - so http://localhost/a.js on an https page is not blocked, which is what a browser does.
    if (!is_potentially_trustworthy(document_url) || is_potentially_trustworthy(resource_url))
        return true;
    if (lower(resource_url.scheme()) != "http")
        return true;
    // Optionally-blockable content: images are the one category this engine fetches and renders, so they are
    // allowed; scripts, styles, documents and form submissions are blockable and are not.
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

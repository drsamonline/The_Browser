#include "content_security_policy.hpp"
#include "security_policy.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace aetheris::rendering {

namespace {

std::string lower_copy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::vector<std::string> split_on_space(std::string const& value)
{
    std::vector<std::string> parts;
    std::istringstream stream(value);
    std::string part;
    while (stream >> part)
        parts.push_back(part);
    return parts;
}

// "host:port" -> ("host", "port"), with an empty port when none was written.
std::pair<std::string, std::string> host_and_port(std::string authority)
{
    auto const colon = authority.rfind(':');
    if (colon == std::string::npos)
        return { lower_copy(std::move(authority)), {} };
    // A bare IPv6 literal has no port to strip: [::1] keeps its brackets.
    if (authority.find(']') != std::string::npos && authority.find(']') > colon)
        return { lower_copy(std::move(authority)), {} };
    return { lower_copy(authority.substr(0, colon)), authority.substr(colon + 1) };
}

} // namespace

ContentSecurityPolicy ContentSecurityPolicy::parse(std::string_view policy)
{
    ContentSecurityPolicy result;
    result.m_raw = std::string(policy);

    std::size_t start = 0;
    while (start <= policy.size()) {
        auto const semicolon = policy.find(';', start);
        auto const directive = policy.substr(start,
            semicolon == std::string_view::npos ? std::string_view::npos : semicolon - start);
        start = semicolon == std::string_view::npos ? policy.size() + 1 : semicolon + 1;

        std::size_t index = 0;
        while (index < directive.size() && std::isspace(static_cast<unsigned char>(directive[index])))
            ++index;
        std::string name;
        while (index < directive.size() && !std::isspace(static_cast<unsigned char>(directive[index])))
            name += static_cast<char>(std::tolower(static_cast<unsigned char>(directive[index++])));
        if (name.empty())
            continue;
        // The value keeps its case: a source list may hold a path, and a path is case-sensitive.
        std::string value(directive.substr(index));
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
            value.pop_back();
        // A repeated directive is ignored after the first, as the spec says.
        bool seen = false;
        for (auto const& existing : result.m_directives)
            if (existing.first == name)
                seen = true;
        if (!seen)
            result.m_directives.emplace_back(name, value);
    }
    return result;
}

std::string ContentSecurityPolicy::effective_source_list(std::string_view directive) const
{
    for (auto const& [name, value] : m_directives)
        if (name == directive)
            return value;
    for (auto const& [name, value] : m_directives)
        if (name == "default-src")
            return value;
    return {};
}

bool ContentSecurityPolicy::allows_fetch(std::string_view directive, Url const& url, Url const& document_url) const
{
    // No policy, or no directive that governs this fetch: allowed. The absence of a restriction is not a refusal.
    bool have_directive = false;
    for (auto const& [name, value] : m_directives) {
        (void)value;
        if (name == directive || name == "default-src")
            have_directive = true;
    }
    if (!have_directive)
        return true;

    auto const list = effective_source_list(directive);
    auto const sources = split_on_space(list);
    if (sources.empty())
        return true; // `img-src;` with nothing after it restricts nothing

    for (auto const& source : sources)
        if (source_matches(source, url, document_url))
            return true;
    return false;
}

bool ContentSecurityPolicy::allows_inline_script() const
{
    bool have_directive = false;
    for (auto const& [name, value] : m_directives) {
        (void)value;
        if (name == "script-src" || name == "default-src")
            have_directive = true;
    }
    if (!have_directive)
        return true;
    auto const list = lower_copy(effective_source_list("script-src"));
    if (list.find("'none'") != std::string::npos)
        return false;
    return list.find("'unsafe-inline'") != std::string::npos;
}

bool ContentSecurityPolicy::allows_inline_style() const
{
    bool have_directive = false;
    for (auto const& [name, value] : m_directives) {
        (void)value;
        if (name == "style-src" || name == "default-src")
            have_directive = true;
    }
    if (!have_directive)
        return true;
    auto const list = lower_copy(effective_source_list("style-src"));
    if (list.find("'none'") != std::string::npos)
        return false;
    return list.find("'unsafe-inline'") != std::string::npos;
}

bool ContentSecurityPolicy::source_matches(std::string const& source, Url const& url, Url const& document_url)
{
    auto const expression = lower_copy(source);
    if (expression.empty())
        return false;
    if (expression == "'none'")
        return false;
    if (expression == "*")
        return true;
    if (expression == "'self'")
        return SecurityPolicy::is_same_origin(url, document_url);

    auto const url_scheme = lower_copy(url.scheme());
    auto const [url_host, url_port] = host_and_port(url.authority());

    // A scheme source: `https:`, `http:`.
    if (expression.size() > 1 && expression.back() == ':'
        && expression.find("://") == std::string::npos) {
        return expression.substr(0, expression.size() - 1) == url_scheme;
    }

    // A host source, with or without a scheme: `example.com`, `*.example.com`, `https://example.com:8443`.
    std::string scheme;
    std::string host_part = expression;
    auto const scheme_end = expression.find("://");
    if (scheme_end != std::string::npos) {
        scheme = expression.substr(0, scheme_end);
        host_part = expression.substr(scheme_end + 3);
    }
    // A path in the source is not matched against the request's path: this engine serves whole documents and its
    // resource URLs rarely carry paths that matter. Recorded as a limit rather than silently half-done.
    auto const slash = host_part.find('/');
    if (slash != std::string::npos)
        host_part = host_part.substr(0, slash);

    auto const [source_host, source_port] = host_and_port(host_part);
    if (source_host.empty())
        return false;
    if (!scheme.empty() && scheme != url_scheme)
        return false;
    if (!source_port.empty() && source_port != url_port)
        return false;

    if (source_host.size() > 2 && source_host.rfind("*.", 0) == 0) {
        auto const suffix = source_host.substr(1); // ".example.com"
        return url_host.size() > suffix.size() && url_host.compare(url_host.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
    return source_host == url_host;
}

} // namespace aetheris::rendering

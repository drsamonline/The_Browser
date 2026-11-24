#include "resource_loader.hpp"

#include <cstdio>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <regex>

namespace aetheris::rendering {

ResourceLoader::ResourceLoader(ResourceCache& cache, Fetcher fetcher)
    : m_cache(cache)
    , m_fetcher(std::move(fetcher))
{
}

ResourceLoader::ResourceLoader(ResourceCache& cache, std::vector<std::shared_ptr<ResourceBackend>> backends)
    : m_cache(cache)
    , m_backends(std::move(backends))
{
}

void ResourceLoader::add_backend(std::shared_ptr<ResourceBackend> backend)
{
    if (backend)
        m_backends.push_back(std::move(backend));
}

ResourceLoadResult ResourceLoader::load(Url const& url)
{
    if (!url.is_valid())
        return { ResourceLoadState::Failed, nullptr, "Invalid resource URL" };
    if (auto cached = m_cache.get(url.serialized()))
        return { ResourceLoadState::Ready, std::move(cached), {} };
    if (m_fetcher) {
        auto fetched = m_fetcher(url);
        if (fetched) {
            if (fetched->url.empty())
                fetched->url = url.serialized();
            if (fetched->url != url.serialized())
                return { ResourceLoadState::Failed, nullptr, "Loaded resource URL does not match request" };
            m_cache.put(std::move(*fetched));
            return { ResourceLoadState::Ready, m_cache.get(url.serialized()), {} };
        }
    }

    bool supported = false;
    std::string failure;
    for (auto const& backend : m_backends) {
        if (!backend || !backend->supports(url))
            continue;
        supported = true;
        auto result = backend->load(url);
        if (result.succeeded()) {
            auto fetched = std::move(*result.resource);
            if (fetched.url.empty())
                fetched.url = url.serialized();
            if (fetched.url != url.serialized())
                return { ResourceLoadState::Failed, nullptr, "Loaded resource URL does not match request" };
            m_cache.put(std::move(fetched));
            return { ResourceLoadState::Ready, m_cache.get(url.serialized()), {} };
        }
        if (!result.message.empty())
            failure = std::move(result.message);
    }
    if (!supported)
        return { ResourceLoadState::Missing, nullptr, "Resource is not cached and no backend supports the URL" };
    return { ResourceLoadState::Missing, nullptr, failure.empty() ? "Resource backend did not provide the requested resource" : failure };
}

ResourceLoadResult ResourceLoader::load(ResourceType expected_type, Url const& url)
{
    auto result = load(url);
    if (!result.succeeded())
        return result;
    if (result.resource->type != expected_type)
        return { ResourceLoadState::Failed, nullptr, "Loaded resource has an unexpected type" };
    return result;
}

namespace {

// The five entities an attribute value can carry that change the meaning of a URL.
std::string decode_entities(std::string const& text)
{
    if (text.find('&') == std::string::npos)
        return text;
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out.push_back(text[i]);
            continue;
        }
        auto const matches = [&](char const* entity) {
            auto const length = std::strlen(entity);
            return text.compare(i, length, entity) == 0;
        };
        if (matches("&amp;")) {
            out.push_back('&');
            i += 4;
        } else if (matches("&lt;")) {
            out.push_back('<');
            i += 3;
        } else if (matches("&gt;")) {
            out.push_back('>');
            i += 3;
        } else if (matches("&quot;")) {
            out.push_back('"');
            i += 5;
        } else if (matches("&#39;")) {
            out.push_back('\'');
            i += 4;
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

} // namespace

std::vector<Url> ResourceLoader::discover_dependencies(ResourceData const& document, Url const& base_url) const
{
    std::vector<Url> urls;
    for (auto const& dependency : discover_dependencies_detailed(document, base_url))
        urls.push_back(dependency.url);
    return urls;
}

std::vector<ResourceDependency> ResourceLoader::discover_dependencies_detailed(ResourceData const& document, Url const& base_url) const
{
    std::vector<ResourceDependency> urls;
    if (document.type != ResourceType::Document)
        return urls;
    auto text = std::string(document.bytes.begin(), document.bytes.end());

    // Walk the tags, not the text. This used to take every `href` or `src` in the document, so every
    // `<a href>` became a dependency and a page's entire link graph was downloaded: the wikipedia
    // article named 1,324 dependencies, of which 1,136 were hyperlinks the page never loads (row 0.5).
    //
    // A hyperlink is navigation; `<link href>` is a subresource. Only the second is fetched here.
    std::regex const tag(R"(<\s*([a-zA-Z][a-zA-Z0-9-]*)((?:[^>\"']|\"[^\"]*\"|'[^']*')*)>)");
    std::regex const attribute(R"(\b(href|src)\s*=\s*[\"']([^\"']+)[\"'])", std::regex::icase);

    auto lower = [](std::string text) {
        for (auto& c : text)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };

    // `<base href>` rebases every relative URL in the document. It is the first one in the head that
    // counts, and without it a page served from a subdirectory resolves its assets against the wrong
    // directory (row 0.5's tail).
    Url resolution_base = base_url;
    std::regex const base_tag(R"(<\s*base\s[^>]*\bhref\s*=\s*[\"']([^\"']+)[\"'])", std::regex::icase);
    if (std::smatch base_match; std::regex_search(text, base_match, base_tag)) {
        auto rebased = Url::resolve(base_url, decode_entities(base_match[1].str()));
        if (rebased.is_valid())
            resolution_base = std::move(rebased);
    }

    // Only a scheme a backend can serve is worth queueing. A wikipedia page carries 47
    // `mw-data:TemplateStyles:...` links, one per transcluded template style, and every one of them
    // was fetched and failed (row 0.5's tail).
    auto const fetchable = [](Url const& url) {
        auto const& scheme = url.scheme();
        return scheme == "http" || scheme == "https" || scheme == "file" || scheme == "data";
    };

    for (std::sregex_iterator it(text.begin(), text.end(), tag), end; it != end; ++it) {
        std::string const name = lower((*it)[1].str());
        bool const hyperlink = name == "a" || name == "area";
        std::string const attributes = (*it)[2].str();
        for (std::sregex_iterator a(attributes.begin(), attributes.end(), attribute), aend; a != aend; ++a) {
            if (lower((*a)[1].str()) == "href" && hyperlink)
                continue;
            // An attribute value is HTML text, so `&amp;` in a query string has to become `&` before it
            // is a URL. Without this every `load.php?...&amp;...` stylesheet 404s - which is why a real
            // page rendered unstyled, and it stayed invisible while hyperlinks were being fetched
            // instead (row 0.5).
            auto raw = decode_entities((*a)[2].str());
            if (std::getenv("AETHERIS_TRACE_DOC"))
                std::fprintf(stderr, "STYLE dep raw=[%s]\n", raw.c_str());
            auto resolved = Url::resolve(resolution_base, raw);
            if (!resolved.is_valid() || !fetchable(resolved))
                continue;
            // `href` is only kept on `<link>`, which is how a stylesheet is declared; `src` is an image,
            // a script or a font.
            bool const from_href = lower((*a)[1].str()) == "href";
            urls.push_back({ std::move(resolved), from_href });
        }
    }
    return urls;
}

} // namespace aetheris::rendering

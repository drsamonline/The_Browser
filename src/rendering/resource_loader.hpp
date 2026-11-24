#pragma once

#include "resource.hpp"
#include "resource_backend.hpp"
#include "url.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace aetheris::rendering {

enum class ResourceLoadState { Ready, Missing, Failed };

struct ResourceLoadResult {
    ResourceLoadState state { ResourceLoadState::Missing };
    std::shared_ptr<ResourceData const> resource;
    std::string message;
    bool succeeded() const { return state == ResourceLoadState::Ready && resource; }
};

// A URL the document refers to, and what it is for. The kind has to travel with the URL: a stylesheet
// served by a script endpoint (`/w/load.php?...&only=styles`) has no `.css` to test for, and reading the
// suffix instead meant a real page's 41 stylesheets were fetched, cached as images and never entered the
// cascade at all (row 0.7).
struct ResourceDependency {
    Url url;
    bool is_stylesheet { false };
};

class ResourceLoader {
public:
    using Fetcher = std::function<std::optional<ResourceData>(Url const&)>;

    explicit ResourceLoader(ResourceCache&, Fetcher fetcher = {});
    explicit ResourceLoader(ResourceCache&, std::vector<std::shared_ptr<ResourceBackend>> backends);
    void add_backend(std::shared_ptr<ResourceBackend>);
    ResourceCache& cache() { return m_cache; }
    ResourceLoadResult load(Url const&);
    ResourceLoadResult load(ResourceType expected_type, Url const&);
    std::vector<Url> discover_dependencies(ResourceData const& document, Url const& base_url) const;
    // The same list with each URL's kind, for a host that has to fetch them differently.
    std::vector<ResourceDependency> discover_dependencies_detailed(ResourceData const& document, Url const& base_url) const;

private:
    ResourceCache& m_cache;
    Fetcher m_fetcher;
    std::vector<std::shared_ptr<ResourceBackend>> m_backends;
};

} // namespace aetheris::rendering

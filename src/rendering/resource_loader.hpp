#pragma once

#include "resource.hpp"
#include "url.hpp"

#include <functional>
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

class ResourceLoader {
public:
    using Fetcher = std::function<std::optional<ResourceData>(Url const&)>;

    explicit ResourceLoader(ResourceCache&, Fetcher fetcher = {});
    ResourceLoadResult load(Url const&);
    ResourceLoadResult load(ResourceType expected_type, Url const&);
    std::vector<Url> discover_dependencies(ResourceData const& document, Url const& base_url) const;

private:
    ResourceCache& m_cache;
    Fetcher m_fetcher;
};

} // namespace aetheris::rendering

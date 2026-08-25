#pragma once

#include "resource.hpp"
#include "url.hpp"

#include <optional>
#include <string>

namespace aetheris::rendering {

enum class ResourceBackendState { Ready, Unsupported, Missing, Failed };

struct ResourceBackendResult {
    ResourceBackendState state { ResourceBackendState::Unsupported };
    std::optional<ResourceData> resource;
    std::string message;
    bool succeeded() const { return state == ResourceBackendState::Ready && resource.has_value(); }
};

class ResourceBackend {
public:
    virtual ~ResourceBackend() = default;
    virtual bool supports(Url const&) const = 0;
    virtual ResourceBackendResult load(Url const&) = 0;
};

} // namespace aetheris::rendering

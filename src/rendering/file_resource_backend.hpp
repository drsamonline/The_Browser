#pragma once

#include "resource_backend.hpp"

namespace aetheris::rendering {

class FileResourceBackend final : public ResourceBackend {
public:
    bool supports(Url const&) const override;
    ResourceBackendResult load(Url const&) override;
};

} // namespace aetheris::rendering

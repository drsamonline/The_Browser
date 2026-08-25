#pragma once

#include "resource.hpp"
#include "url.hpp"

#include <string_view>

namespace aetheris::rendering {

struct ContentType {
    ResourceType resource_type { ResourceType::Document };
    std::string_view mime_type;
};

ContentType classify_content_type(Url const&, std::string_view declared_mime_type = {});

} // namespace aetheris::rendering

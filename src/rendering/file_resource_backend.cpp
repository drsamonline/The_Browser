#include "file_resource_backend.hpp"
#include "content_type.hpp"

#include <filesystem>
#include <fstream>

namespace aetheris::rendering {

bool FileResourceBackend::supports(Url const& url) const
{
    return url.scheme() == "file";
}

ResourceBackendResult FileResourceBackend::load(Url const& url)
{
    if (!supports(url))
        return { ResourceBackendState::Unsupported, std::nullopt, "File backend only supports file:// URLs" };
    std::filesystem::path path(url.path());
    if (path.empty())
        return { ResourceBackendState::Missing, std::nullopt, "File URL has no path" };
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec)
        return { ResourceBackendState::Missing, std::nullopt, "Requested file does not exist" };
    if (!std::filesystem::is_regular_file(path, ec) || ec)
        return { ResourceBackendState::Failed, std::nullopt, "Requested file is not a regular file" };
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return { ResourceBackendState::Failed, std::nullopt, "Unable to open requested file" };
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (!input.eof() && input.fail())
        return { ResourceBackendState::Failed, std::nullopt, "Unable to read requested file" };
    auto content_type = classify_content_type(url);
    ResourceData resource { content_type.resource_type, url.serialized(), std::move(bytes) };
    return { ResourceBackendState::Ready, std::move(resource), {} };
}

} // namespace aetheris::rendering

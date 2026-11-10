#include "file_resource_backend.hpp"
#include "content_type.hpp"

#include <cctype>
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
    auto raw_path = url.path();
#if defined(_WIN32)
    // A Windows file URL is file:///C:/dir/file, and path() gives "/C:/dir/file".
    // std::filesystem::path reads a leading separator as "root of the current
    // drive", so that became \C:\dir\file and every file:// load failed with
    // "does not exist" — which is why the resource test asserted and then sat in
    // the CRT's abort dialog until ctest killed it at 180 s.
    if (raw_path.size() >= 3 && raw_path[0] == '/'
        && std::isalpha(static_cast<unsigned char>(raw_path[1])) && raw_path[2] == ':')
        raw_path.erase(raw_path.begin());
#endif
    std::filesystem::path path(raw_path);
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

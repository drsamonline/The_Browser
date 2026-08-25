#include "content_type.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace aetheris::rendering {

static std::string lower(std::string_view value)
{
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

ContentType classify_content_type(Url const& url, std::string_view declared_mime_type)
{
    auto mime = lower(declared_mime_type);
    auto classify_mime = [](std::string const& value) -> ContentType {
        if (value == "text/html" || value == "application/xhtml+xml") return { ResourceType::Document, "text/html" };
        if (value == "text/css") return { ResourceType::Stylesheet, "text/css" };
        if (value.starts_with("image/")) return { ResourceType::Image, "image/*" };
        if (value.starts_with("font/") || value == "application/font-woff" || value == "application/font-woff2") return { ResourceType::Font, "font/*" };
        return { ResourceType::Binary, "application/octet-stream" };
    };
    if (!mime.empty())
        return classify_mime(mime.substr(0, mime.find(';')));

    auto path = lower(url.path());
    auto dot = path.rfind('.');
    auto extension = dot == std::string::npos ? std::string {} : path.substr(dot + 1);
    if (extension == "html" || extension == "htm" || extension == "xhtml") return { ResourceType::Document, "text/html" };
    if (extension == "css") return { ResourceType::Stylesheet, "text/css" };
    if (extension == "ppm" || extension == "png" || extension == "jpg" || extension == "jpeg" || extension == "gif" || extension == "webp") return { ResourceType::Image, "image/*" };
    if (extension == "ttf" || extension == "otf" || extension == "woff" || extension == "woff2") return { ResourceType::Font, "font/*" };
    return { ResourceType::Binary, "application/octet-stream" };
}

} // namespace aetheris::rendering

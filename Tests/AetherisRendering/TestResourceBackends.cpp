#include "content_type.hpp"
#include "file_resource_backend.hpp"
#include "navigation.hpp"
#include "resource_loader.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <memory>

using namespace aetheris::rendering;

int main()
{
    auto temp = std::filesystem::temp_directory_path() / "aetheris_resource_backend_test";
    std::filesystem::create_directories(temp);
    auto html = temp / "index.html";
    auto css = temp / "site.css";
    { std::ofstream out(html); out << "<div>backend document</div>"; }
    { std::ofstream out(css); out << "div { width: 80px; height: 20px; }"; }

    // Three slashes on Windows: a path there is C:/Users/..., so "file://" + path
    // gives "file://C:/Users/..." and the parser reads "C:" as the HOST. The
    // loader then tries to fetch that host and the test hangs on the resolve
    // (Timeout 180 on the first Windows run that reached this point).
    auto file_url = [](std::filesystem::path const& path) {
#if defined(_WIN32)
        return Url("file:///" + path.generic_string());
#else
        return Url("file://" + path.generic_string());
#endif
    };
    Url html_url = file_url(html);
    Url css_url = file_url(css);
    assert(classify_content_type(html_url).resource_type == ResourceType::Document);
    assert(classify_content_type(css_url).resource_type == ResourceType::Stylesheet);
    assert(classify_content_type(Url("file:///tmp/blob.bin")).resource_type == ResourceType::Binary);

    ResourceCache cache;
    ResourceLoader loader(cache, std::vector<std::shared_ptr<ResourceBackend>> { std::make_shared<FileResourceBackend>() });
    auto document = loader.load(ResourceType::Document, html_url);
    assert(document.succeeded());
    assert(cache.contains(html_url.serialized()));
    auto stylesheet = loader.load(ResourceType::Stylesheet, css_url);
    assert(stylesheet.succeeded());
    auto again = loader.load(html_url);
    assert(again.succeeded());
    auto missing = loader.load(Url("file:///definitely/not/aetheris-resource.html"));
    assert(!missing.succeeded());

    NavigationController navigation;
    NavigationRequest request { .document_url = html_url, .stylesheet_url = css_url, .viewport_width = 640.0f };
    auto result = navigation.navigate(request, loader);
    assert(result.succeeded());
    assert(navigation.has_document());
    assert(navigation.current_url().serialized() == html_url.serialized());

    std::filesystem::remove_all(temp);
    return 0;
}

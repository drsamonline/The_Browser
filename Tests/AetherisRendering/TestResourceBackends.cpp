#include "content_type.hpp"
#include "file_resource_backend.hpp"
#include "navigation.hpp"
#include "resource_loader.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>

using namespace aetheris::rendering;

// Progress markers. This test timed out at 180 s on Windows with no output at all,
// which says nothing about where it was. Each step prints and flushes before it
// runs, so the next run names the step that hung instead of the whole file.
static void step(char const* name)
{
    std::fprintf(stderr, "[step] %s\n", name);
    std::fflush(stderr);
}

int main()
{
    step("temp dir");

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
    step("file urls");
    Url html_url = file_url(html);
    Url css_url = file_url(css);
    step("content types");
    assert(classify_content_type(html_url).resource_type == ResourceType::Document);
    assert(classify_content_type(css_url).resource_type == ResourceType::Stylesheet);
    assert(classify_content_type(Url("file:///tmp/blob.bin")).resource_type == ResourceType::Binary);

    step("loader construct");
    ResourceCache cache;
    ResourceLoader loader(cache, std::vector<std::shared_ptr<ResourceBackend>> { std::make_shared<FileResourceBackend>() });
    step("url accessors");
    { auto p = html_url.path(); auto sz = html_url.serialized(); std::fprintf(stderr, "[step]   path=%s serialized=%s\n", p.c_str(), sz.c_str()); std::fflush(stderr); }
    step("backend supports");
    { FileResourceBackend backend; std::fprintf(stderr, "[step]   supports=%d\n", (int)backend.supports(html_url)); std::fflush(stderr); }
    step("backend load");
    { FileResourceBackend backend; auto r = backend.load(html_url); std::fprintf(stderr, "[step]   state=%d\n", (int)r.state); std::fflush(stderr); }
    step("classify");
    { auto ct = classify_content_type(html_url); std::fprintf(stderr, "[step]   type=%d\n", (int)ct.resource_type); std::fflush(stderr); }
    step("load document");
    auto document = loader.load(ResourceType::Document, html_url);
    assert(document.succeeded());
    assert(cache.contains(html_url.serialized()));
    step("load stylesheet");
    auto stylesheet = loader.load(ResourceType::Stylesheet, css_url);
    assert(stylesheet.succeeded());
    auto again = loader.load(html_url);
    assert(again.succeeded());
    step("load missing");
    auto missing = loader.load(Url("file:///definitely/not/aetheris-resource.html"));
    assert(!missing.succeeded());

    step("navigate");
    NavigationController navigation;
    NavigationRequest request { .document_url = html_url, .stylesheet_url = css_url, .viewport_width = 640.0f };
    auto result = navigation.navigate(request, loader);
    assert(result.succeeded());
    assert(navigation.has_document());
    assert(navigation.current_url().serialized() == html_url.serialized());

    step("cleanup");
    std::filesystem::remove_all(temp);
    std::fprintf(stderr, "[step] done\n");
    return 0;
}

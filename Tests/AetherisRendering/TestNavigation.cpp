#include "navigation.hpp"

#include <cassert>
#include <iostream>

using namespace aetheris::rendering;

int main()
{
    auto absolute = Url::parse("https://example.test/docs/index.html");
    assert(absolute.is_absolute());
    assert(absolute.scheme() == "https");
    assert(absolute.authority() == "example.test");
    assert(absolute.path() == "/docs/index.html");

    assert(Url::resolve(absolute, "site.css").serialized() == "https://example.test/docs/site.css");
    assert(Url::resolve(absolute, "/assets/site.css").serialized() == "https://example.test/assets/site.css");
    assert(Url::resolve(absolute, "https://cdn.example.test/site.css").serialized() == "https://cdn.example.test/site.css");

    ResourceCache resources;
    resources.put_text(ResourceType::Document,
        "https://example.test/docs/index.html", "<div>First document</div>");
    resources.put_text(ResourceType::Document,
        "https://example.test/docs/second.html", "<div>Second document</div>");
    resources.put_text(ResourceType::Stylesheet,
        "https://example.test/docs/site.css", "div { width: 120px; height: 20px; }");

    NavigationController navigation;

    NavigationRequest first {
        .document_url = absolute,
        .stylesheet_url = Url::parse("site.css"),
        .viewport_width = 640.0f,
    };

    auto result = navigation.navigate(first, resources);
    assert(result.succeeded());
    assert(navigation.has_document());
    assert(navigation.current_url().serialized() == "https://example.test/docs/index.html");
    assert(navigation.current_document());
    assert(navigation.current_document()->source_url() == "https://example.test/docs/index.html");

    NavigationRequest second {
        .document_url = Url::parse("https://example.test/docs/second.html"),
        .stylesheet_url = Url::parse("site.css"),
        .viewport_width = 640.0f,
    };

    result = navigation.navigate(second, resources);
    assert(result.succeeded());
    assert(navigation.current_url().serialized() == "https://example.test/docs/second.html");
    assert(navigation.current_document()->source_url() == "https://example.test/docs/second.html");

    NavigationRequest missing {
        .document_url = Url::parse("https://example.test/docs/missing.html"),
        .stylesheet_url = Url::parse("site.css"),
        .viewport_width = 640.0f,
    };
    result = navigation.navigate(missing, resources);
    assert(result.error == NavigationError::MissingDocument);
    assert(navigation.current_url().serialized() == "https://example.test/docs/second.html");

    std::cout << "Navigation tests passed\n";
    return 0;
}

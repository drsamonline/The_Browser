#include "render_document.hpp"

#include <cassert>
#include <iostream>

using namespace aetheris::rendering;

int main()
{
    ResourceCache resources;
    resources.put_text(ResourceType::Document, "memory://index.html", "<div>Hello Aetheris</div>");
    resources.put_text(ResourceType::Stylesheet, "memory://site.css", "div { width: 120px; height: 20px; }");

    auto rendered = RenderDocument::create_from_resources("memory://index.html", "memory://site.css", 640.0f, resources);
    assert(rendered.has_value());
    assert(rendered->source_url() == "memory://index.html");
    assert(rendered->resources().contains("memory://index.html"));
    assert(rendered->resources().contains("memory://site.css"));
    assert(rendered->resources().size() == 2);

    auto surface = rendered->render_to_surface(640, 480);
    assert(surface.width() == 640);
    assert(surface.height() == 480);

    ResourceCache incomplete;
    incomplete.put_text(ResourceType::Document, "memory://index.html", "<div></div>");
    assert(!RenderDocument::create_from_resources("memory://index.html", "memory://missing.css", 640.0f, incomplete));

    std::cout << "Document resource integration tests passed\n";
    return 0;
}

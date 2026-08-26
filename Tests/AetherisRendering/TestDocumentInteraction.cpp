#include "browser_session.hpp"
#include "resource_loader.hpp"

#include <cassert>
#include <optional>
#include <unordered_map>

using namespace aetheris::rendering;

static ResourceData document_resource(std::string url, std::string html)
{
    return { ResourceType::Document, std::move(url), std::vector<unsigned char>(html.begin(), html.end()) };
}

static ResourceData stylesheet_resource(std::string url)
{
    std::string css = R"(
body { display: block; }
a { display: block; height: 30px; }
#target { display: block; height: 500px; }
)";
    return { ResourceType::Stylesheet, std::move(url), std::vector<unsigned char>(css.begin(), css.end()) };
}

int main()
{
    ResourceCache cache;
    std::unordered_map<std::string, ResourceData> resources;
    resources.emplace("https://example.test/a.html", document_resource("https://example.test/a.html", R"(<html><head><title>A</title><link rel="stylesheet" href="site.css"></head><body><a href="b.html">Go B</a><a href="#target">Jump</a><div id="target">Target</div></body></html>)"));
    resources.emplace("https://example.test/b.html", document_resource("https://example.test/b.html", R"(<html><head><title>B</title><link rel="stylesheet" href="site.css"></head><body>B</body></html>)"));
    resources.emplace("https://example.test/site.css", stylesheet_resource("https://example.test/site.css"));

    ResourceLoader loader(cache, [&](Url const& url) -> std::optional<ResourceData> {
        auto it = resources.find(url.serialized());
        if (it == resources.end())
            return std::nullopt;
        return it->second;
    });

    BrowserSession session;
    session.set_viewport(800, 100);
    auto loaded = session.navigate({ Url::parse("https://example.test/a.html"), std::nullopt, 800 }, loader);
    assert(loaded.succeeded());

    auto link = session.hit_test(1, 1);
    assert(link.hit());
    assert(link.is_link());
    assert(link.link_url->serialized() == "https://example.test/b.html");

    auto activate = session.activate_at(1, 1, loader);
    assert(activate.succeeded());
    assert(session.current_page());
    assert(session.current_page()->title == "B");
    assert(session.history_size() == 2);

    auto back = session.back(loader);
    assert(back.succeeded());
    assert(session.current_page()->title == "A");

    auto fragment = session.scroll_to_fragment("target");
    assert(fragment);
    assert(session.viewport().scroll_y() >= 0);

    session.scroll_by(0, 10000);
    auto before = session.viewport().scroll_y();
    session.scroll_by(0, 10000);
    assert(session.viewport().scroll_y() == before);

    auto empty = session.activate_at(700, 90, loader);
    assert(!empty.succeeded());
    assert(session.current_page()->title == "A");
}

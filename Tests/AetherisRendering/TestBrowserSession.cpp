#include "browser_session.hpp"
#include "resource_loader.hpp"

#include <cassert>
#include <unordered_map>

using namespace aetheris::rendering;

static ResourceData make_document(std::string url, std::string html)
{
    return { ResourceType::Document, std::move(url), std::vector<unsigned char>(html.begin(), html.end()) };
}

static ResourceData make_stylesheet(std::string url)
{
    std::string css = "body { display: block; }";
    return { ResourceType::Stylesheet, std::move(url), std::vector<unsigned char>(css.begin(), css.end()) };
}

int main()
{
    ResourceCache cache;
    std::unordered_map<std::string, ResourceData> resources;
    resources.emplace("https://example.test/a.html", make_document("https://example.test/a.html", "<html><head><title>Page A</title><link rel=\"stylesheet\" href=\"site.css\"></head><body>A</body></html>"));
    resources.emplace("https://example.test/b.html", make_document("https://example.test/b.html", "<html><head><title>Page B</title><link rel=\"stylesheet\" href=\"site.css\"></head><body>B</body></html>"));
    resources.emplace("https://example.test/c.html", make_document("https://example.test/c.html", "<html><head><title>Page C</title><link rel=\"stylesheet\" href=\"site.css\"></head><body>C</body></html>"));
    resources.emplace("https://example.test/site.css", make_stylesheet("https://example.test/site.css"));

    ResourceLoader loader(cache, [&](Url const& url) -> std::optional<ResourceData> {
        auto it = resources.find(url.serialized());
        if (it == resources.end())
            return std::nullopt;
        return it->second;
    });

    BrowserSession session;
    auto a = session.navigate({ Url::parse("https://example.test/a.html"), std::nullopt, 800 }, loader);
    assert(a.succeeded());
    assert(session.history_size() == 1);
    assert(session.current_history_index() && *session.current_history_index() == 0);
    assert(session.current_page() && session.current_page()->title == "Page A");

    auto b = session.navigate({ Url::parse("https://example.test/b.html"), std::nullopt, 800 }, loader);
    assert(b.succeeded());
    assert(session.can_go_back());
    assert(session.history_size() == 2);
    assert(session.current_page()->title == "Page B");

    auto back = session.back(loader);
    assert(back.succeeded());
    assert(session.current_history_index() && *session.current_history_index() == 0);
    assert(session.can_go_forward());
    assert(session.current_page()->title == "Page A");

    auto forward = session.forward(loader);
    assert(forward.succeeded());
    assert(session.current_history_index() && *session.current_history_index() == 1);
    assert(session.current_page()->title == "Page B");

    auto back_again = session.back(loader);
    assert(back_again.succeeded());
    auto c = session.navigate({ Url::parse("https://example.test/c.html"), std::nullopt, 800 }, loader);
    assert(c.succeeded());
    assert(session.history_size() == 2);
    assert(!session.can_go_forward());
    assert(session.current_page()->title == "Page C");

    auto failed = session.navigate({ Url::parse("https://example.test/missing.html"), std::nullopt, 800 }, loader);
    assert(!failed.succeeded());
    assert(session.history_size() == 2);
    assert(session.current_page()->title == "Page C");

    auto reload = session.reload(loader);
    assert(reload.succeeded());
    assert(session.history_size() == 2);
    assert(session.current_page()->title == "Page C");
}

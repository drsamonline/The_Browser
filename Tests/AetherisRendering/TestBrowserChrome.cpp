#include "browser_application.hpp"
#include "browser_chrome.hpp"
#include "resource_loader.hpp"

#include <cassert>
#include <string>

using namespace aetheris::rendering;

// NOTE: This suite intentionally avoids the upstream LibTest framework;
// AetherisRendering tests are standalone executables using plain asserts,
// matching the convention of the other files in Tests/AetherisRendering.

static void basic_chrome_state()
{
    BrowserApplication app;
    BrowserChrome chrome(app);

    assert(chrome.tab_count() == 1u);
    assert(chrome.set_address("https://example.test/"));
    assert(chrome.address() == "https://example.test/");

    auto second = chrome.new_tab();
    assert(chrome.tab_count() == 2u);
    assert(chrome.activate_tab(second));
    assert(chrome.close_active_tab());
    assert(chrome.tab_count() == 1u);
}

static void chrome_navigation_uses_address_and_updates_page_state()
{
    ResourceCache cache;
    cache.put_text(ResourceType::Document,
        "https://example.test/",
        "<html><head><title>Aetheris Test</title><link rel=\"stylesheet\" href=\"site.css\"></head><body><p>Hello</p></body></html>");
    cache.put_text(ResourceType::Stylesheet,
        "https://example.test/site.css",
        "p { width: 100px; height: 20px; }");

    ResourceLoader loader(cache);
    BrowserApplication app(800.0f, 600.0f);
    BrowserChrome chrome(app);

    assert(chrome.set_address("https://example.test/"));
    auto result = chrome.navigate(loader);

    assert(result.succeeded());
    assert(chrome.page_state() == BrowserChrome::PageState::Ready);
    assert(chrome.status_text() == "Ready");

    auto* session = app.active_session();
    assert(session);
    assert(session->current_page());
    assert(session->current_page()->url.serialized() == "https://example.test/");
    assert(session->current_page()->title == "Aetheris Test");
}

static void chrome_navigation_reports_errors()
{
    ResourceCache cache;
    ResourceLoader loader(cache);
    BrowserApplication app;
    BrowserChrome chrome(app);

    assert(chrome.set_address("https://missing.example/"));
    auto result = chrome.navigate(loader);

    assert(!result.succeeded());
    assert(chrome.page_state() == BrowserChrome::PageState::Error);
    assert(!chrome.status_text().empty());
}

int main()
{
    basic_chrome_state();
    chrome_navigation_uses_address_and_updates_page_state();
    chrome_navigation_reports_errors();
    return 0;
}

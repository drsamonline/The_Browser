#include <LibTest/TestCase.h>

#include "browser_application.hpp"
#include "browser_chrome.hpp"
#include "resource_loader.hpp"

using namespace aetheris::rendering;

TEST_CASE(basic_chrome_state)
{
    BrowserApplication app;
    BrowserChrome chrome(app);

    EXPECT_EQ(chrome.tab_count(), 1u);
    EXPECT(chrome.set_address("https://example.test/"));
    EXPECT_EQ(chrome.address(), "https://example.test/");

    auto second = chrome.new_tab();
    EXPECT_EQ(chrome.tab_count(), 2u);
    EXPECT(chrome.activate_tab(second));
    EXPECT(chrome.close_active_tab());
    EXPECT_EQ(chrome.tab_count(), 1u);
}

TEST_CASE(chrome_navigation_uses_address_and_updates_page_state)
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

    EXPECT(chrome.set_address("https://example.test/"));
    auto result = chrome.navigate(loader);

    EXPECT(result.succeeded());
    EXPECT_EQ(chrome.page_state(), BrowserChrome::PageState::Ready);
    EXPECT_EQ(chrome.status_text(), "Ready");

    auto* session = app.active_session();
    EXPECT(session);
    EXPECT(session->current_page());
    EXPECT_EQ(session->current_page()->url.serialized(), "https://example.test/");
    EXPECT_EQ(session->current_page()->title, "Aetheris Test");
}

TEST_CASE(chrome_navigation_reports_errors)
{
    ResourceCache cache;
    ResourceLoader loader(cache);
    BrowserApplication app;
    BrowserChrome chrome(app);

    EXPECT(chrome.set_address("https://missing.example/"));
    auto result = chrome.navigate(loader);

    EXPECT(!result.succeeded());
    EXPECT_EQ(chrome.page_state(), BrowserChrome::PageState::Error);
    EXPECT(!chrome.status_text().empty());
}

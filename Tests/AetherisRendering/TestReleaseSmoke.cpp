#include <cassert>

#include "browser_application.hpp"
#include "browser_chrome.hpp"
#include "resource_loader.hpp"

using namespace aetheris::rendering;

int main()
{
    ResourceCache cache;

    cache.put_text(ResourceType::Document,
        "https://release.example/first.html",
        "<html><head><title>First</title><link rel=\"stylesheet\" href=\"site.css\"></head>"
        "<body><p>First page</p></body></html>");
    cache.put_text(ResourceType::Document,
        "https://release.example/second.html",
        "<html><head><title>Second</title><link rel=\"stylesheet\" href=\"site.css\"></head>"
        "<body><p>Second page</p></body></html>");
    cache.put_text(ResourceType::Stylesheet,
        "https://release.example/site.css",
        "p { width: 120px; height: 20px; }");

    ResourceLoader loader(cache);
    BrowserApplication application(800.0f, 600.0f);
    BrowserChrome chrome(application);

    assert(chrome.set_address("https://release.example/first.html"));
    auto first = chrome.navigate(loader);
    assert(first.succeeded());
    assert(chrome.page_state() == BrowserChrome::PageState::Ready);
    assert(application.active_session());
    assert(application.active_session()->current_page());
    assert(application.active_session()->current_page()->title == "First");

    assert(chrome.set_address("https://release.example/second.html"));
    auto second = chrome.navigate(loader);
    assert(second.succeeded());
    assert(chrome.can_go_back());

    auto back = chrome.back(loader);
    assert(back.succeeded());
    assert(application.active_session()->current_page());
    assert(application.active_session()->current_page()->title == "First");
    assert(chrome.can_go_forward());

    auto forward = chrome.forward(loader);
    assert(forward.succeeded());
    assert(application.active_session()->current_page());
    assert(application.active_session()->current_page()->title == "Second");

    auto second_tab = chrome.new_tab();
    assert(chrome.activate_tab(second_tab));
    assert(chrome.set_address("https://release.example/first.html"));
    assert(chrome.navigate(loader).succeeded());
    assert(application.active_session()->current_page());
    assert(application.active_session()->current_page()->title == "First");

    assert(chrome.activate_tab(0));
    assert(application.active_session()->current_page());
    assert(application.active_session()->current_page()->title == "Second");

    return 0;
}

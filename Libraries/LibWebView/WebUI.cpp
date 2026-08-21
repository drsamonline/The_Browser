/*
 * Copyright (c) 2025, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibIPC/Transport.h>
#include <LibIPC/TransportHandle.h>
#include <LibWebView/WebContentClient.h>
#include <LibWebView/WebUI.h>
#if AETHERIS_ENABLE_BOOKMARKS_UI
#include <LibWebView/WebUI/BookmarksUI.h>
#endif
#if AETHERIS_ENABLE_DOWNLOADS_UI
#include <LibWebView/WebUI/DownloadsUI.h>
#endif
#if AETHERIS_ENABLE_HISTORY_UI
#include <LibWebView/WebUI/HistoryUI.h>
#endif
#include <LibWebView/WebUI/SettingsUI.h>
#include <LibWebView/WebUI/VersionUI.h>

namespace WebView {

static constexpr auto s_pages = to_array<WebUI::Page>({
    { "about"sv, "About URLs"sv, WebUI::PageType::Static },
#if AETHERIS_ENABLE_BOOKMARKS_UI
    { "bookmarks"sv, "Bookmarks"sv, WebUI::PageType::Dynamic },
#endif
#if AETHERIS_ENABLE_DOWNLOADS_UI
    { "downloads"sv, "Downloads"sv, WebUI::PageType::Dynamic },
#endif
#if AETHERIS_ENABLE_HISTORY_UI
    { "history"sv, "History"sv, WebUI::PageType::Dynamic },
#endif
    { "newtab"sv, "New Tab"sv, WebUI::PageType::Static },
    { "settings"sv, "Settings"sv, WebUI::PageType::Dynamic },
    { "version"sv, "Version"sv, WebUI::PageType::Dynamic },
});

ReadonlySpan<WebUI::Page> WebUI::pages()
{
    return s_pages;
}

Optional<WebUI::Page const&> WebUI::page_for_host(StringView host)
{
    for (auto const& page : s_pages) {
        if (page.host == host)
            return page;
    }
    return {};
}

template<typename WebUIType>
static ErrorOr<NonnullRefPtr<WebUIType>> create_web_ui(WebContentClient& client, u64 page_id, String host)
{
    VERIFY(page_id > 0);

    auto paired = TRY(IPC::Transport::create_paired());
    auto handle = move(paired.remote_handle);

    auto web_ui = WebUIType::create(client, move(paired.local), move(host));
    client.async_connect_to_web_ui(page_id, move(handle));

    return web_ui;
}

ErrorOr<RefPtr<WebUI>> WebUI::create(WebContentClient& client, u64 page_id, String host)
{
    auto page = page_for_host(host);
    if (!page.has_value() || page->type == PageType::Static)
        return nullptr;

    RefPtr<WebUI> web_ui;

#if AETHERIS_ENABLE_BOOKMARKS_UI
    if (page->host == "bookmarks"sv)
        web_ui = TRY(create_web_ui<BookmarksUI>(client, page_id, move(host)));
#endif
#if AETHERIS_ENABLE_DOWNLOADS_UI
    if (!web_ui && page->host == "downloads"sv)
        web_ui = TRY(create_web_ui<DownloadsUI>(client, page_id, move(host)));
#endif
#if AETHERIS_ENABLE_HISTORY_UI
    if (!web_ui && page->host == "history"sv)
        web_ui = TRY(create_web_ui<HistoryUI>(client, page_id, move(host)));
#endif
    if (!web_ui && page->host == "settings"sv)
        web_ui = TRY(create_web_ui<SettingsUI>(client, page_id, move(host)));
    else if (!web_ui && page->host == "version"sv)
        web_ui = TRY(create_web_ui<VersionUI>(client, page_id, move(host)));

    VERIFY(web_ui);
    web_ui->register_interfaces();

    return web_ui;
}

WebUI::WebUI(WebContentClient& client, NonnullOwnPtr<IPC::Transport> transport, String host)
    : IPC::ConnectionToServer<WebUIClientEndpoint, WebUIServerEndpoint>(*this, move(transport))
    , m_client(client)
    , m_host(move(host))
{
}

WebUI::~WebUI() = default;

void WebUI::die()
{
    m_client.web_ui_disconnected({});
}

void WebUI::register_interface(StringView name, Interface interface)
{
    auto result = m_interfaces.set(name, move(interface));
    VERIFY(result == HashSetResult::InsertedNewEntry);
}

void WebUI::received_message(String name, JsonValue data)
{
    auto interface = m_interfaces.get(name);
    if (!interface.has_value()) {
        warnln("Received message from WebUI for unrecognized interface: {}", name);
        return;
    }

    interface.value()(move(data));
}

}

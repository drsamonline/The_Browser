/**
 * AETHERIS BROWSER - MAIN ENTRY POINT
 * Cross-platform hyper-optimized browser
 */

#include "aetheris/core.hpp"
#include "aetheris/network/socket_abstraction.hpp"
#include "aetheris/network/http_client.hpp"
#include "aetheris/cache/ghost_cache.hpp"
#include "aetheris/ui/window_manager.hpp"

#include "css_parser.hpp"
#include "render_document.hpp"
#include "url_path.hpp"
#include "style.hpp"
#include "image_output.hpp"
#include "http_resource_backend.hpp"
#include "resource_backend.hpp"
#include "file_resource_backend.hpp"
#include "resource_loader.hpp"
#include "secure_resource_loader.hpp"
#include "ui/browser_session.hpp"
#include "ui/chrome_layout.hpp"
#include "ui/library_page.hpp"
#include "ui/input_router.hpp"
#include "ui/window_host.hpp"
#include "resource.hpp"
#include "url.hpp"

#include "js.hpp"
#include "document_interaction.hpp"
#include "viewport.hpp"
#include "dom_mutation.hpp"
#include "paint_executor.hpp"
#include "scroll_bar.hpp"

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <regex>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace aetheris;

// Debug console overlay renderer
class DebugOverlay {
public:
    void update_stats(
        double fps,
        size_t memory_usage,
        size_t cache_entries,
        float compression_ratio
    ) {
        m_fps = fps;
        m_memory_mb = memory_usage / (1024.0 * 1024.0);
        m_cache_entries = cache_entries;
        m_compression_ratio = compression_ratio;
        
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer),
            "AETHERIS DEBUG CONSOLE\n"
            "FPS: %.1f | Memory: %.2f MB | Cache: %zu entries | Compression: %.2fx\n"
            "Press ESC to close | F11 for fullscreen",
            m_fps, m_memory_mb, m_cache_entries, m_compression_ratio
        );
        m_overlay_text = buffer;
    }
    
    const std::string& get_text() const noexcept {
        return m_overlay_text;
    }
    
private:
    double m_fps{0.0};
    double m_memory_mb{0.0};
    size_t m_cache_entries{0};
    float m_compression_ratio{1.0f};
    std::string m_overlay_text;
};

// Headless smoke mode: run a bounded number of frames and exit with status 0.
// Used by CI (`aetheris --smoke-test`) where no display server is attached to
// the process environment.
//
// NOTE: GhostCache embeds multi-megabyte block pools as members, so these
// objects must never live on the stack (stack overflow => SIGSEGV before any
// output). Allocate them on the heap instead.
static int run_smoke_test() {
    auto net_error = network::Socket::initialize();
    if (net_error != network::SocketError::None) {
        std::cerr << "Failed to initialize network subsystem" << std::endl;
        return 1;
    }

    auto window = std::make_unique<ui::WindowManager>();
    ui::WindowConfig config;
    config.width = 1280;
    config.height = 720;
    config.title = "Aetheris Browser (smoke test)";

    // A CI runner has no display server. Creating a window there is expected to
    // fail, so fall back to the headless path instead of failing the smoke test:
    // the point is to prove the subsystems initialise, not to paint a window.
    bool has_window = window->create(config);
    if (!has_window)
        std::cout << "No display server; running the headless smoke path." << std::endl;

    auto ghost_cache = std::make_unique<cache::GhostCache>();

    constexpr int kMaxSmokeFrames = 10;
    int frames = 0;
    if (has_window) {
        window->show();
        for (; frames < kMaxSmokeFrames; ++frames) {
            // Poll first: on the X11 backend a failed poll (no display server,
            // e.g. CI without xvfb) must abort the loop instead of driving the
            // render path with uninitialized platform state.
            if (!window->poll_events()) {
                break;
            }

            window->clear();
            window->draw_debug_overlay("AETHERIS SMOKE TEST");
            window->swap_buffers();
        }
    }

    // Even without a window, exercise the parts that must work everywhere: the
    // HTML parser, the cascade and layout.
    auto parsed = rendering::Document::parse_html(
        "<html><body><h1>smoke</h1><p style='color:red'>text</p></body></html>");
    auto rendered = rendering::RenderDocument::create_from_document(std::move(parsed), "", 800.0f);
    auto surface = rendered.render_to_surface(200, 100, rendering::Color { 255, 255, 255, 255 });
    bool painted = !surface.pixels().empty();

    std::cout << "Smoke test finished after " << frames << " frame(s)." << std::endl;
    std::cout << "  Display: " << (has_window ? "window" : "headless") << std::endl;
    std::cout << "  Active cache entries: " << ghost_cache->active_entries() << std::endl;
    std::cout << "  Layout and paint: " << (painted ? "ok" : "failed") << std::endl;

    ghost_cache->clear_all();
    window->destroy();
    network::Socket::cleanup();

    if (!painted) {
        std::cerr << "Smoke test failed: the engine could not lay out and paint a document." << std::endl;
        return 1;
    }

    std::cout << "Aetheris smoke test passed." << std::endl;
    return 0;
}

// Headless fetch mode: resolve and GET a URL over the network, then report the
// result. Validates the networking path without needing a display server.
// Every network fetch goes through SecureResourceLoader so the security policy
// that the tests exercise is the one a real page actually gets.
namespace {

struct SecureFetcher {
    rendering::ResourceCache cache;
    std::shared_ptr<rendering::HttpResourceBackend> backend;
    std::shared_ptr<rendering::ResourceLoader> loader;
    std::shared_ptr<rendering::SecureResourceLoader> secure;

    explicit SecureFetcher(rendering::Url const& document_url)
    {
        backend = std::make_shared<rendering::HttpResourceBackend>([](rendering::HttpRequest const& request) {
            rendering::HttpResponse out;
            auto response = network::HttpClient::get(request.url.serialized());
            out.status = response.status;
            out.body.assign(response.body.begin(), response.body.end());
            out.message = response.error;
            out.content_type = response.header("Content-Type");
            for (auto const& header : response.headers)
                out.headers[header.name] = header.value;
            return out;
        });
        // The file backend is registered alongside HTTP so a local document renders the same way a fetched one does -
        // which is what the coverage corpus needs: fetch with curl, render with the engine (row 0.1).
        loader = std::make_shared<rendering::ResourceLoader>(cache,
            std::vector<std::shared_ptr<rendering::ResourceBackend>> {
                std::make_shared<rendering::FileResourceBackend>(), backend });
        secure = std::make_shared<rendering::SecureResourceLoader>(*loader, document_url);
    }

    rendering::ResourceLoadResult fetch(rendering::ResourceRequestContext context, rendering::Url const& url)
    {
        return secure->load(context, url);
    }
};

} // namespace

static void serialize_html(rendering::DomNode const& node, std::string& out);

// Prints the parsed DOM after scripts have run, so layout/DOM bugs can be
// inspected without rendering an image.
static int run_dump_dom(std::string const& url) {
    if (network::Socket::initialize() != network::SocketError::None) {
        std::cerr << "Failed to initialize network subsystem" << std::endl;
        return 1;
    }
    rendering::Url document_url = rendering::Url::parse(url);
    SecureFetcher fetcher(document_url);
    auto loaded = fetcher.fetch(rendering::ResourceRequestContext::Document, document_url);
    network::Socket::cleanup();

    if (!loaded.succeeded()) {
        std::cerr << "Fetch failed: " << loaded.message << std::endl;
        return 1;
    }
    std::string body(loaded.resource->bytes.begin(), loaded.resource->bytes.end());

    auto document = rendering::Document::parse_html(body);
    std::string out;
    serialize_html(document.root(), out);
    std::cout << out << std::endl;
    return 0;
}

static int run_fetch(std::string const& url) {
    if (network::Socket::initialize() != network::SocketError::None) {
        std::cerr << "Failed to initialize network subsystem" << std::endl;
        return 1;
    }

    rendering::Url document_url = rendering::Url::parse(url);
    SecureFetcher fetcher(document_url);
    auto loaded = fetcher.fetch(rendering::ResourceRequestContext::Document, document_url);
    network::Socket::cleanup();

    if (!loaded.succeeded()) {
        std::cerr << "Fetch failed: " << loaded.message << std::endl;
        return 1;
    }
    std::cout << "OK " << loaded.resource->url << std::endl;
    std::cout << "  Bytes: " << loaded.resource->bytes.size() << std::endl;
    return 0;
}



static rendering::DomNode* find_element(rendering::DomNode& node, std::string const& name)
{
    if (node.type == rendering::DomNodeType::Element && node.name == name)
        return &node;
    for (auto& child : node.children)
        if (auto* found = find_element(*child, name))
            return found;
    return nullptr;
}

static std::string node_text(rendering::DomNode const& node)
{
    std::string result;
    if (node.type == rendering::DomNodeType::Text)
        result += node.data;
    for (auto const& child : node.children)
        result += node_text(*child);
    return result;
}

static void set_node_text(rendering::DomNode& node, std::string const& text)
{
    // A text write, not an attribute write: it clears the recorded attribute name, so
    // relayout() can tell this can reflow and never skips the pass for it.
    rendering::note_text_mutation(&node);
    node.children.clear();
    node.append_child(std::make_unique<rendering::DomNode>(rendering::DomNodeType::Text, "", text));
}

// File-local: used by the DOM manipulation entry points below and nowhere else.
// Without `static` this is an external symbol with no previous declaration,
// which -Wmissing-declarations (and -Werror) reject.
static std::unique_ptr<rendering::DomNode> detach_child(rendering::DomNode& parent, rendering::DomNode* child)
{
    for (auto it = parent.children.begin(); it != parent.children.end(); ++it) {
        if (it->get() == child) {
            auto node = std::move(*it);
            parent.children.erase(it);
            return node;
        }
    }
    return nullptr;
}

// Serialises a DOM subtree back to HTML (for innerHTML).
static void serialize_html(rendering::DomNode const& node, std::string& out)
{
    switch (node.type) {
    case rendering::DomNodeType::Text:
        out += node.data;
        break;
    case rendering::DomNodeType::Comment:
        out += "<!--" + node.data + "-->";
        break;
    case rendering::DomNodeType::Doctype:
        out += "<!DOCTYPE html>";
        break;
    case rendering::DomNodeType::Element:
        out += "<" + node.name;
        for (auto const& [name, value] : node.attributes)
            out += " " + name + "=\"" + value + "\"";
        out += ">";
        for (auto const& child : node.children)
            serialize_html(*child, out);
        out += "</" + node.name + ">";
        break;
    default:
        for (auto const& child : node.children)
            serialize_html(*child, out);
        break;
    }
}

// The declarations in an element's style attribute, in the order they appear. Upstream exposes the
// same list as CSSStyleDeclaration's length/item (CSS/CSSStyleDeclaration.idl), so the accessors
// below are the declared surface and not an invention.
static std::vector<std::pair<std::string, std::string>> parse_declarations(rendering::DomNode const* node)
{
    std::vector<std::pair<std::string, std::string>> declarations;
    auto* existing = node->attribute("style");
    if (!existing)
        return declarations;
    std::istringstream stream(*existing);
    std::string declaration;
    while (std::getline(stream, declaration, ';')) {
        auto colon = declaration.find(':');
        if (colon == std::string::npos)
            continue;
        std::string name = declaration.substr(0, colon);
        std::string text = declaration.substr(colon + 1);
        auto trim = [](std::string& s) {
            auto b = s.find_first_not_of(" \t\r\n");
            auto e = s.find_last_not_of(" \t\r\n");
            s = b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
        };
        trim(name);
        trim(text);
        if (!name.empty())
            declarations.emplace_back(name, text);
    }
    return declarations;
}

// Writes the list back, through the attribute choke point so the renderer hears about it.
static void write_declarations(rendering::DomNode* node, std::vector<std::pair<std::string, std::string>> const& declarations)
{
    std::string joined;
    for (auto const& entry : declarations) {
        if (entry.second.empty())
            continue;
        if (!joined.empty())
            joined += "; ";
        joined += entry.first + ": " + entry.second;
    }
    rendering::set_attribute(*node, "style", joined);
}

// Sets one declaration in an element's style attribute, preserving the rest.
static void apply_style_property(rendering::DomNode* node, std::string const& property, std::string const& value)
{
    auto declarations = parse_declarations(node);
    bool replaced = false;
    for (auto& entry : declarations)
        if (entry.first == property) {
            entry.second = value;
            replaced = true;
        }
    if (!replaced && !value.empty())
        declarations.emplace_back(property, value);
    write_declarations(node, declarations);
}

// ---- JavaScript DOM host -----------------------------------------------------
// Element handles are real QuickJS objects that carry the DomNode* as opaque
// data, so scripts interact with genuine JS values rather than a bespoke model.

namespace {

// One browser per process, so the class id and host are file-static.
JSClassID g_dom_node_class { 0 };
rendering::CssStyleSheet const* g_stylesheet { nullptr };
rendering::DomNode* g_document_root { nullptr };
aetheris::js::Interpreter* g_js { nullptr };
struct DomHost;
DomHost* g_dom_host { nullptr };

// Resolves an href against the page it was found on. Absolute stays absolute, a leading slash takes the
// scheme and host, anything else replaces the last path segment. Not RFC 3986 - no dot-segment removal,
// no query merging - which is enough for a link and stated here rather than implied. A dot-segment
// implementation was written on 2026-11-18, hung the browser on `../up`, and was reverted: see
// docs/CHECKLIST.md row 1g5.
static std::string resolve_url(std::string const& base, std::string const& href)
{
    if (href.rfind("http://", 0) == 0 || href.rfind("https://", 0) == 0)
        return href;
    auto const scheme_end = base.find("://");
    if (scheme_end == std::string::npos)
        return href;
    auto const host_end = base.find('/', scheme_end + 3);
    std::string const origin = host_end == std::string::npos ? base : base.substr(0, host_end);
    if (!href.empty() && href.front() == '/')
        return origin + rendering::remove_dot_segments(href);
    auto const slash = base.find_last_of('/');
    std::string const directory = slash == std::string::npos || slash < scheme_end + 3 ? origin + "/" : base.substr(0, slash + 1);
    return origin + rendering::remove_dot_segments(directory.substr(origin.size()) + href);
}

// The page the window loop is currently driving. `getBoundingClientRect()` needs the layout tree and
// has nothing but a handle to work from, so the loop publishes this the way it publishes the DOM host.
rendering::RenderDocument const* g_current_page { nullptr };

// The URL the current page was loaded from. `location.href` reads it and a relative href resolves
// against it.
std::string g_current_url;

// `element.focus()` and `element.blur()` are called while a script is running, where the window loop's
// document, interpreter and interaction state are not in scope. They record a request here and the loop
// performs it with set_focus(), the same helper the keyboard uses - so a scripted focus fires the same
// blur/focus events and re-resolves style the same way.
// A queue, not a slot. run_timers() runs every timer due in one frame's budget, so a script that calls
// focus() and then blur() inside that window used to have the first request overwritten before the loop
// performed it - the focus state moved for neither, and no focus event fired. Measured 2026-11-18.
std::vector<rendering::DomNode*> g_focus_requests;

// Navigation requested from script or from a click. The loop performs it, because the session and the
// page-dirty flag live there. Empty means nothing pending; a reload request re-fetches the same URL.
std::string g_navigation_request;
bool g_navigation_pending { false };
bool g_navigation_reload { false };
bool g_navigation_replace { false };

struct DomHost {
    JSContext* context { nullptr };
    std::map<rendering::DomNode*, JSValue> handle_by_node; // owned references
    std::map<std::string, JSValue> handle_by_id;           // owned references
    std::map<rendering::DomNode*, rendering::DomNode*> parent_of;
    std::map<rendering::DomNode*, std::unique_ptr<rendering::DomNode>> pending;

    void initialize(JSContext* ctx)
    {
        context = ctx;
        g_dom_host = this;
        if (g_dom_node_class == 0) {
            JS_NewClassID(JS_GetRuntime(ctx), &g_dom_node_class);
            JSClassDef definition {};
            definition.class_name = "DomNode";
            definition.finalizer = nullptr;
            JS_NewClass(JS_GetRuntime(ctx), g_dom_node_class, &definition);
        }
    }

    JSValue handle_for(rendering::DomNode* node);
    void collect(rendering::DomNode& node);
    void register_subtree(rendering::DomNode& node);
    void purge_subtree(rendering::DomNode& node);
    void adopt(rendering::DomNode* parent, rendering::DomNode* child);

    void release()
    {
        if (!context)
            return;
        for (auto& [node, value] : handle_by_node)
            JS_FreeValue(context, value);
        handle_by_node.clear();
        // handle_by_id holds its own references, so they must be released too.
        for (auto& [id, value] : handle_by_id)
            JS_FreeValue(context, value);
        handle_by_id.clear();
        if (g_dom_host == this)
            g_dom_host = nullptr;
    }
};

rendering::DomNode* node_of(JSContext* context, JSValueConst value)
{
    (void)context;
    return static_cast<rendering::DomNode*>(JS_GetOpaque(value, g_dom_node_class));
}

// ---- element methods -------------------------------------------------------

// ---- form controls ---------------------------------------------------------

bool node_is(rendering::DomNode const* node, char const* name)
{
    return node && node->type == rendering::DomNodeType::Element && node->name == name;
}

// The value of a control, with the DOM as the source of truth: <input> uses its
// value attribute, <textarea> its text, <select> the selected <option>.
std::string control_value(rendering::DomNode const* node)
{
    if (!node)
        return {};
    if (node_is(node, "textarea"))
        return node_text(*node);
    if (node_is(node, "option")) {
        if (auto* value = node->attribute("value"))
            return *value;
        return node_text(*node);
    }
    if (node_is(node, "select")) {
        rendering::DomNode const* first = nullptr;
        for (auto const& child : node->children) {
            if (!node_is(child.get(), "option"))
                continue;
            if (!first)
                first = child.get();
            if (child->has_attribute("selected"))
                return control_value(child.get());
        }
        return first ? control_value(first) : std::string();
    }
    if (auto* value = node->attribute("value"))
        return *value;
    return {};
}

void set_control_value(rendering::DomNode* node, std::string const& value)
{
    if (!node)
        return;
    if (node_is(node, "textarea")) {
        if (g_dom_host)
            g_dom_host->purge_subtree(*node);
        set_node_text(*node, value);
        return;
    }
    if (node_is(node, "option") || node_is(node, "select")) {
        rendering::set_attribute(*node, "value", value);
        return;
    }
    rendering::set_attribute(*node, "value", value);
}

JSValue js_value_get(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    auto* node = node_of(context, this_value);
    return JS_NewString(context, control_value(node).c_str());
}

JSValue js_value_set(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    auto* node = node_of(context, this_value);
    if (!node)
        return JS_UNDEFINED;
    const char* raw = argc > 0 ? JS_ToCString(context, argv[0]) : nullptr;
    set_control_value(node, raw ? raw : "");
    if (raw)
        JS_FreeCString(context, raw);
    return JS_UNDEFINED;
}

JSValue js_checked_get(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    auto* node = node_of(context, this_value);
    return JS_NewBool(context, node && node->has_attribute("checked"));
}

JSValue js_checked_set(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    auto* node = node_of(context, this_value);
    if (!node)
        return JS_UNDEFINED;
    bool checked = argc > 0 && JS_ToBool(context, argv[0]) > 0;
    if (checked)
        rendering::set_attribute(*node, "checked", "");
    else
        rendering::remove_attribute(*node, "checked");
    return JS_UNDEFINED;
}

// form.submit(): fires a cancelable submit event. Nothing navigates yet, so the
// observable behaviour is that preventDefault() can stop it.
JSValue js_form_submit(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    auto* node = node_of(context, this_value);
    if (!node || !g_dom_host || !g_js)
        return JS_UNDEFINED;
    auto handle = g_dom_host->handle_by_node.find(node);
    if (handle == g_dom_host->handle_by_node.end())
        return JS_UNDEFINED;
    g_js->dispatch_event_to(JS_DupValue(context, handle->second), "submit");
    if (g_js->last_event_prevented())
        return JS_UNDEFINED;
    return JS_UNDEFINED;
}

JSValue js_children_get(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    auto* node = node_of(context, this_value);
    JSValue array = JS_NewArray(context);
    if (!node || !g_dom_host)
        return array;
    uint32_t index = 0;
    for (auto& child : node->children) {
        if (child->type != rendering::DomNodeType::Element)
            continue;
        auto it = g_dom_host->handle_by_node.find(child.get());
        if (it != g_dom_host->handle_by_node.end())
            JS_SetPropertyUint32(context, array, index++, JS_DupValue(context, it->second));
    }
    return array;
}

JSValue js_parent_node_get(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    auto* node = node_of(context, this_value);
    if (!node || !g_dom_host)
        return JS_NULL;
    auto parent = g_dom_host->parent_of.find(node);
    if (parent == g_dom_host->parent_of.end())
        return JS_NULL;
    auto handle = g_dom_host->handle_by_node.find(parent->second);
    if (handle == g_dom_host->handle_by_node.end())
        return JS_NULL;
    return JS_DupValue(context, handle->second);
}

JSValue js_element_tag_name(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    auto* node = node_of(context, this_value);
    if (!node)
        return JS_UNDEFINED;
    std::string name = node->name;
    for (auto& c : name)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return JS_NewString(context, name.c_str());
}

// camelCase dataset key -> data-* attribute name (xY -> data-x-y).
std::string dataset_attribute_name(std::string const& key)
{
    std::string name = "data-";
    for (char c : key) {
        if (std::isupper(static_cast<unsigned char>(c))) {
            name += '-';
            name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            name += c;
        }
    }
    return name;
}

std::string to_string_arg(JSContext* context, JSValueConst value)
{
    const char* raw = JS_ToCString(context, value);
    std::string text = raw ? raw : "";
    if (raw)
        JS_FreeCString(context, raw);
    return text;
}

JSValue js_dataset_get(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    if (argc < 2)
        return JS_UNDEFINED;
    auto* node = node_of(context, argv[0]);
    if (!node)
        return JS_UNDEFINED;
    std::string name = dataset_attribute_name(to_string_arg(context, argv[1]));
    auto* value = node->attribute(name);
    return value ? JS_NewString(context, value->c_str()) : JS_UNDEFINED;
}

JSValue js_dataset_set(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    if (argc < 3)
        return JS_FALSE;
    auto* node = node_of(context, argv[0]);
    if (!node)
        return JS_FALSE;
    std::string name = dataset_attribute_name(to_string_arg(context, argv[1]));
    const char* raw = JS_ToCString(context, argv[2]);
    rendering::set_attribute(*node, name, raw ? raw : "");
    if (raw)
        JS_FreeCString(context, raw);
    return JS_TRUE;
}

JSValue js_dataset_has(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    if (argc < 2)
        return JS_FALSE;
    auto* node = node_of(context, argv[0]);
    if (!node)
        return JS_FALSE;
    return JS_NewBool(context, node->has_attribute(dataset_attribute_name(to_string_arg(context, argv[1]))));
}

// Writable view of the data-* attributes: a proxy so any key works, not just
// the ones that existed when the object was built.
JSValue js_element_dataset(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    auto* node = node_of(context, this_value);
    JSValue target = JS_NewObjectClass(context, g_dom_node_class);
    JS_SetOpaque(target, node);

    JSValue handler = JS_NewObject(context);
    JS_SetPropertyStr(context, handler, "get", JS_NewCFunction(context, js_dataset_get, "get", 3));
    JS_SetPropertyStr(context, handler, "set", JS_NewCFunction(context, js_dataset_set, "set", 4));
    JS_SetPropertyStr(context, handler, "has", JS_NewCFunction(context, js_dataset_has, "has", 2));

    JSValue proxy = JS_NewProxy(context, target, handler);
    JS_FreeValue(context, target);
    JS_FreeValue(context, handler);
    return proxy;
}

JSValue js_element_remove(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    auto* node = node_of(context, this_value);
    if (!node || !g_dom_host)
        return JS_UNDEFINED;
    auto parent = g_dom_host->parent_of.find(node);
    if (parent == g_dom_host->parent_of.end())
        return JS_UNDEFINED;
    g_dom_host->purge_subtree(*node);
    auto detached = detach_child(*parent->second, node);
    rendering::note_child_list_mutation_at(parent->second);
    g_dom_host->parent_of.erase(node);
    return JS_UNDEFINED;
}

JSValue js_element_insert_before(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    auto* node = node_of(context, this_value);
    if (!node || argc < 1 || !g_dom_host)
        return JS_UNDEFINED;
    auto* child = node_of(context, argv[0]);
    auto* reference = argc > 1 ? node_of(context, argv[1]) : nullptr;
    if (!child)
        return JS_UNDEFINED;

    std::unique_ptr<rendering::DomNode> moved;
    auto pending_it = g_dom_host->pending.find(child);
    if (pending_it != g_dom_host->pending.end()) {
        moved = std::move(pending_it->second);
        g_dom_host->pending.erase(pending_it);
    } else {
        auto parent_it = g_dom_host->parent_of.find(child);
        if (parent_it != g_dom_host->parent_of.end())
            moved = detach_child(*parent_it->second, child);
        if (!moved)
            return JS_UNDEFINED;
    }

    if (reference) {
        auto position = std::find_if(node->children.begin(), node->children.end(),
            [reference](std::unique_ptr<rendering::DomNode> const& entry) { return entry.get() == reference; });
        node->children.insert(position, std::move(moved));
        g_dom_host->parent_of[child] = node;
    } else {
        node->append_child(std::move(moved));
        g_dom_host->parent_of[child] = node;
    }
    return JS_UNDEFINED;
}

JSValue js_computed_property(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    auto* node = node_of(context, this_value);
    if (!node || !g_stylesheet || argc < 1)
        return JS_NewString(context, "");
    const char* raw = JS_ToCString(context, argv[0]);
    std::string property = raw ? raw : "";
    if (raw)
        JS_FreeCString(context, raw);
    rendering::StyleResolver resolver;
    auto properties = resolver.resolve(*node, *g_stylesheet);
    auto* value = properties.get(property);
    return JS_NewString(context, value ? value->c_str() : "");
}

JSValue js_get_computed_style(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    JSValue style = JS_NewObjectClass(context, g_dom_node_class);
    JS_SetOpaque(style, argc > 0 ? node_of(context, argv[0]) : nullptr);
    JS_SetPropertyStr(context, style, "getPropertyValue",
        JS_NewCFunction(context, js_computed_property, "getPropertyValue", 1));
    return style;
}

JSValue js_document_title_get(JSContext* context, JSValueConst, int, JSValueConst*)
{
    if (!g_document_root)
        return JS_NewString(context, "");
    auto* title = find_element(*g_document_root, "title");
    return JS_NewString(context, title ? node_text(*title).c_str() : "");
}

JSValue js_document_title_set(JSContext* context, JSValueConst, int argc, JSValueConst* argv)
{
    if (!g_document_root)
        return JS_UNDEFINED;
    auto* title = find_element(*g_document_root, "title");
    if (!title)
        return JS_UNDEFINED;
    const char* raw = argc > 0 ? JS_ToCString(context, argv[0]) : nullptr;
    set_node_text(*title, raw ? raw : "");
    if (raw)
        JS_FreeCString(context, raw);
    return JS_UNDEFINED;
}

JSValue js_set_attribute(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    auto* node = node_of(context, this_value);
    if (!node || argc < 2) {
        rendering::note_dom_mutation();
        return JS_UNDEFINED;
    }
    const char* name = JS_ToCString(context, argv[0]);
    const char* value = JS_ToCString(context, argv[1]);
    if (name && value)
        rendering::set_attribute(*node, name, value);
    if (name)
        JS_FreeCString(context, name);
    if (value)
        JS_FreeCString(context, value);
    return JS_UNDEFINED;
}

JSValue js_get_attribute(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    auto* node = node_of(context, this_value);
    if (!node || argc < 1)
        return JS_NULL;
    const char* name = JS_ToCString(context, argv[0]);
    auto* value = name ? node->attribute(name) : nullptr;
    JSValue result = value ? JS_NewString(context, value->c_str()) : JS_NULL;
    if (name)
        JS_FreeCString(context, name);
    return result;
}

JSValue js_append_child(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    auto* node = node_of(context, this_value);
    if (!node || argc < 1)
        return JS_UNDEFINED;
    auto* child = node_of(context, argv[0]);
    if (child && g_dom_host)
        g_dom_host->adopt(node, child);
    return JS_UNDEFINED;
}

JSValue js_element_add_event_listener(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    if (argc < 2 || !JS_IsFunction(context, argv[1]))
        return JS_UNDEFINED;
    bool capture = false;
    if (argc > 2) {
        if (JS_IsBool(argv[2]))
            capture = JS_ToBool(context, argv[2]) > 0;
        else if (JS_IsObject(argv[2])) {
            JSValue option = JS_GetPropertyStr(context, argv[2], "capture");
            capture = JS_ToBool(context, option) > 0;
            JS_FreeValue(context, option);
        }
    }
    const char* type = JS_ToCString(context, argv[0]);
    std::string key = capture ? "__capture_" : "__handlers_";
    key += type ? type : "";
    if (type)
        JS_FreeCString(context, type);

    JSValue existing = JS_GetPropertyStr(context, this_value, key.c_str());
    JSValue list = existing;
    if (!JS_IsArray(list)) {
        JS_FreeValue(context, existing);
        list = JS_NewArray(context);
        JS_SetPropertyStr(context, this_value, key.c_str(), JS_DupValue(context, list));
    }
    // Adding the same listener twice is a no-op, as in the DOM.
    JSValue length_value = JS_GetPropertyStr(context, list, "length");
    uint32_t length = 0;
    JS_ToUint32(context, &length, length_value);
    JS_FreeValue(context, length_value);
    for (uint32_t index = 0; index < length; ++index) {
        JSValue entry = JS_GetPropertyUint32(context, list, index);
        bool same = JS_VALUE_GET_PTR(entry) == JS_VALUE_GET_PTR(argv[1]);
        JS_FreeValue(context, entry);
        if (same) {
            JS_FreeValue(context, list);
            return JS_UNDEFINED;
        }
    }
    JS_SetPropertyUint32(context, list, length, JS_DupValue(context, argv[1]));
    JS_FreeValue(context, list);
    return JS_UNDEFINED;
}

JSValue js_element_remove_event_listener(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    if (argc < 2)
        return JS_UNDEFINED;
    bool capture = false;
    if (argc > 2) {
        if (JS_IsBool(argv[2]))
            capture = JS_ToBool(context, argv[2]) > 0;
        else if (JS_IsObject(argv[2])) {
            JSValue option = JS_GetPropertyStr(context, argv[2], "capture");
            capture = JS_ToBool(context, option) > 0;
            JS_FreeValue(context, option);
        }
    }
    const char* type = JS_ToCString(context, argv[0]);
    std::string key = capture ? "__capture_" : "__handlers_";
    key += type ? type : "";
    if (type)
        JS_FreeCString(context, type);

    JSValue list = JS_GetPropertyStr(context, this_value, key.c_str());
    if (!JS_IsArray(list)) {
        JS_FreeValue(context, list);
        return JS_UNDEFINED;
    }
    JSValue length_value = JS_GetPropertyStr(context, list, "length");
    uint32_t length = 0;
    JS_ToUint32(context, &length, length_value);
    JS_FreeValue(context, length_value);

    JSValue rebuilt = JS_NewArray(context);
    uint32_t kept = 0;
    for (uint32_t index = 0; index < length; ++index) {
        JSValue entry = JS_GetPropertyUint32(context, list, index);
        if (JS_VALUE_GET_PTR(entry) != JS_VALUE_GET_PTR(argv[1]))
            JS_SetPropertyUint32(context, rebuilt, kept++, JS_DupValue(context, entry));
        JS_FreeValue(context, entry);
    }
    JS_SetPropertyStr(context, this_value, key.c_str(), rebuilt);
    JS_FreeValue(context, list);
    return JS_UNDEFINED;
}

// ---- classList -------------------------------------------------------------

std::vector<std::string> read_classes(rendering::DomNode* node)
{
    std::vector<std::string> out;
    auto* value = node->attribute("class");
    if (!value)
        return out;
    std::string current;
    for (char c : *value) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty())
        out.push_back(current);
    return out;
}

// Writes the class attribute and tells the renderer. Order matters, as in js_set_attribute: the
// node-scoped note clears the recorded attribute name, so it goes first.
//
// classList used to write node->attributes directly and note nothing, so classList.add() changed
// the DOM and never restyled anything - a silent no-op in the renderer, which is how a page with
// `#a.on{...}` and `classList.add('on')` stayed blue.
void set_class_attribute(rendering::DomNode& node, std::string const& value)
{
    rendering::set_attribute(node, "class", value);
}

void write_classes(rendering::DomNode* node, std::vector<std::string> const& classes)
{
    std::string joined;
    for (auto const& name : classes) {
        if (!joined.empty())
            joined += ' ';
        joined += name;
    }
    set_class_attribute(*node, joined);
}

// Upstream's DOM/Element.idl has `[Reflect=class] attribute DOMString className`, so this is the
// `class` content attribute read and written, nothing more: absent reads as the empty string.
// Whether an element can take focus, ported from upstream's three-level answer:
//
//   Element::is_focusable()          tabindex parses as an integer (DOM/Element.cpp:6992)
//   HTMLAnchorElement::is_focusable  Base::is_focusable() || has_attribute(href)
//   HTMLInputElement::is_focusable   m_type != Hidden && enabled()  (and select, textarea, button: enabled())
//
// Upstream's base rule is only tabindex; the element kinds add themselves. Upstream also requires
// meets_focusable_area_rendering_requirements() - a layout box - which this does not check, so an element
// with display:none can be focused here. Recorded in docs/CHECKLIST.md.
static bool is_focusable_node(rendering::DomNode const& node)
{
    if (node.type != rendering::DomNodeType::Element)
        return false;

    // The rendering requirement (upstream: Element::meets_focusable_area_rendering_requirements(),
    // DOM/Element.cpp:6757 - connected, not a display:none subtree, visibility:visible). Upstream asks
    // the *style*, so it works before layout; we have no style accessor for an arbitrary node, so this
    // asks the layout node and refuses only what it can prove is hidden:
    //
    //   no layout node  -> allowed. Before the first layout nothing has a box, and a page that calls
    //                      focus() at load must not be refused. Upstream does not require a box either.
    //   display: none   -> refused.
    //   visibility:hidden or collapse -> refused, the third clause of upstream's check.
    if (g_current_page) {
        std::function<rendering::LayoutNode const*(rendering::LayoutNode const&)> find
            = [&](rendering::LayoutNode const& candidate) -> rendering::LayoutNode const* {
            if (candidate.dom_node == &node)
                return &candidate;
            for (auto const& child : candidate.children)
                if (auto const* found = find(*child))
                    return found;
            return nullptr;
        };
        if (auto const* layout = find(g_current_page->layout_root())) {
            if (layout->display == rendering::LayoutDisplay::None)
                return false;
            // The third clause: visibility must be visible (upstream reads it from the inherited
            // style; the layout node carries the resolved style here).
            if (auto const* visibility = layout->style.get("visibility"))
                if (*visibility == "hidden" || *visibility == "collapse")
                    return false;
        }
    }

    // The generic rule, which every element gets.
    if (auto const* tabindex = node.attribute("tabindex")) {
        char* end = nullptr;
        std::strtol(tabindex->c_str(), &end, 10);
        if (end && *end == '\0' && !tabindex->empty())
            return true;
    }

    if (node.name == "a")
        return node.attribute("href") != nullptr;
    if (node.name == "button" || node.name == "select" || node.name == "textarea")
        return node.attribute("disabled") == nullptr;
    if (node.name == "input") {
        auto const* type = node.attribute("type");
        bool const hidden = type && *type == "hidden";
        return !hidden && node.attribute("disabled") == nullptr;
    }
    return false;
}

// Upstream's focusing steps are HTML/Focus.cpp, run_focusing_steps(new_focus_target, fallback_target,
// focus_trigger, scroll_into_view, focus_visible); set_focus() here does the same in the loop, which is
// why this only records the request.
//
// KNOWN DIVERGENCE, found by reading that file on 2026-11-18 after this was written: run_focusing_steps
// maps a non-focusable target through get_focusable_area() and returns if it is still not focusable. This
// focuses whatever it is handed, so `focus()` on a div with no tabindex focuses it where upstream would
// refuse. Row 1f4 in docs/CHECKLIST.md.
//
// The earlier version of this comment cited DOM/Element.cpp, which has no focus() - a citation written to
// fit code already in place rather than one that sent me to the file.
JSValue js_focus(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    // Upstream runs the focusing steps, whose first move is to map a non-focusable target through
    // get_focusable_area() and return if nothing focusable comes back (HTML/Focus.cpp:749). So
    // `focus()` on a div with no tabindex does nothing, which is what this does.
    if (auto* node = node_of(context, this_value)) {
        if (is_focusable_node(*node))
            g_focus_requests.push_back(node);
    }
    return JS_UNDEFINED;
}

JSValue js_blur(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    // Blurring something that is not focused does nothing, which is what the spec says and what a page
    // relies on when it blurs defensively.
    auto* node = node_of(context, this_value);
    if (!node)
        return JS_UNDEFINED;
    g_focus_requests.push_back(nullptr);
    return JS_UNDEFINED;
}

// Upstream: Element::get_bounding_client_rect() returns the layout node's border box through the
// viewport mapping (DOM/Element.cpp, Painting::bounding_client_rect). Here the layout node's rect is
// the border box in document coordinates and the window has no scroll offset in this path, so the
// two coincide. All eight DOMRect members are published, because pages read whichever they like.
JSValue js_get_bounding_client_rect(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    auto* node = node_of(context, this_value);
    // Before the first layout there is no page to measure, and upstream returns an empty rect in the
    // same situation (bounding_client_rect_assuming_layout_clean returns {}). A script that runs at
    // load therefore reads zeros - there is no forced layout read here, which is the one place this
    // differs from upstream's ForcedReadScope.
    if (!node || !g_current_page) {
        JSValue empty = JS_NewObject(context);
        for (auto const* key : { "x", "y", "width", "height", "left", "top", "right", "bottom" })
            JS_SetPropertyStr(context, empty, key, JS_NewFloat64(context, 0));
        return empty;
    }

    rendering::LayoutRect rect {};
    std::function<rendering::LayoutNode const*(rendering::LayoutNode const&)> find
        = [&](rendering::LayoutNode const& candidate) -> rendering::LayoutNode const* {
        if (candidate.dom_node == node)
            return &candidate;
        for (auto const& child : candidate.children)
            if (auto const* found = find(*child))
                return found;
        return nullptr;
    };
    if (auto const* layout = find(g_current_page->layout_root()))
        rect = layout->rect;

    JSValue out = JS_NewObject(context);
    JS_SetPropertyStr(context, out, "x", JS_NewFloat64(context, rect.x));
    JS_SetPropertyStr(context, out, "y", JS_NewFloat64(context, rect.y));
    JS_SetPropertyStr(context, out, "width", JS_NewFloat64(context, rect.width));
    JS_SetPropertyStr(context, out, "height", JS_NewFloat64(context, rect.height));
    JS_SetPropertyStr(context, out, "left", JS_NewFloat64(context, rect.x));
    JS_SetPropertyStr(context, out, "top", JS_NewFloat64(context, rect.y));
    JS_SetPropertyStr(context, out, "right", JS_NewFloat64(context, rect.x + rect.width));
    JS_SetPropertyStr(context, out, "bottom", JS_NewFloat64(context, rect.y + rect.height));
    return out;
}

JSValue js_class_name_get(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    auto* node = node_of(context, this_value);
    if (!node)
        return JS_UNDEFINED;
    if (auto* value = node->attribute("class"))
        return JS_NewString(context, value->c_str());
    return JS_NewString(context, "");
}

JSValue js_class_name_set(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    auto* node = node_of(context, this_value);
    if (!node)
        return JS_UNDEFINED;
    const char* raw = argc > 0 ? JS_ToCString(context, argv[0]) : nullptr;
    set_class_attribute(*node, raw ? raw : "");
    if (raw)
        JS_FreeCString(context, raw);
    return JS_UNDEFINED;
}

JSValue js_class_add(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    auto* node = node_of(context, this_value);
    if (!node)
        return JS_UNDEFINED;
    auto classes = read_classes(node);
    for (int i = 0; i < argc; ++i) {
        std::string name = JS_ToCString(context, argv[i]) ? JS_ToCString(context, argv[i]) : "";
        const char* raw = JS_ToCString(context, argv[i]);
        name = raw ? raw : "";
        if (raw)
            JS_FreeCString(context, raw);
        if (name.empty())
            continue;
        bool found = false;
        for (auto const& existing : classes)
            if (existing == name)
                found = true;
        if (!found)
            classes.push_back(name);
    }
    write_classes(node, classes);
    return JS_UNDEFINED;
}

JSValue js_class_remove(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    auto* node = node_of(context, this_value);
    if (!node)
        return JS_UNDEFINED;
    auto classes = read_classes(node);
    for (int i = 0; i < argc; ++i) {
        const char* raw = JS_ToCString(context, argv[i]);
        std::string name = raw ? raw : "";
        if (raw)
            JS_FreeCString(context, raw);
        for (auto it = classes.begin(); it != classes.end();) {
            if (*it == name)
                it = classes.erase(it);
            else
                ++it;
        }
    }
    write_classes(node, classes);
    return JS_UNDEFINED;
}

JSValue js_class_contains(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    auto* node = node_of(context, this_value);
    if (!node || argc < 1)
        return JS_FALSE;
    const char* raw = JS_ToCString(context, argv[0]);
    std::string name = raw ? raw : "";
    if (raw)
        JS_FreeCString(context, raw);
    for (auto const& existing : read_classes(node))
        if (existing == name)
            return JS_TRUE;
    return JS_FALSE;
}

JSValue js_class_toggle(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    auto* node = node_of(context, this_value);
    if (!node || argc < 1)
        return JS_FALSE;
    const char* raw = JS_ToCString(context, argv[0]);
    std::string name = raw ? raw : "";
    if (raw)
        JS_FreeCString(context, raw);

    auto classes = read_classes(node);
    bool present = false;
    for (auto const& existing : classes)
        if (existing == name)
            present = true;
    if (present) {
        for (auto it = classes.begin(); it != classes.end();) {
            if (*it == name)
                it = classes.erase(it);
            else
                ++it;
        }
    } else {
        classes.push_back(name);
    }
    write_classes(node, classes);
    return present ? JS_FALSE : JS_TRUE;
}

// ---- accessors -------------------------------------------------------------

JSValue js_text_content_get(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    auto* node = node_of(context, this_value);
    if (!node)
        return JS_UNDEFINED;
    return JS_NewString(context, node_text(*node).c_str());
}

JSValue js_text_content_set(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    auto* node = node_of(context, this_value);
    if (!node)
        return JS_UNDEFINED;
    const char* raw = argc > 0 ? JS_ToCString(context, argv[0]) : nullptr;
    std::string text = raw ? raw : "";
    if (raw)
        JS_FreeCString(context, raw);
    if (g_dom_host)
        g_dom_host->purge_subtree(*node);
    set_node_text(*node, text);
    return JS_UNDEFINED;
}

JSValue js_inner_html_get(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    auto* node = node_of(context, this_value);
    if (!node)
        return JS_UNDEFINED;
    std::string out;
    for (auto const& child : node->children)
        serialize_html(*child, out);
    return JS_NewString(context, out.c_str());
}

JSValue js_inner_html_set(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    auto* node = node_of(context, this_value);
    if (!node)
        return JS_UNDEFINED;
    const char* raw = argc > 0 ? JS_ToCString(context, argv[0]) : nullptr;
    std::string html = raw ? raw : "";
    if (raw)
        JS_FreeCString(context, raw);

    if (g_dom_host)
        g_dom_host->purge_subtree(*node);
    node->children.clear();
    if (!html.empty()) {
        auto fragment = rendering::Document::parse_html(html);
        // append_child(), not push_back(): the parsed nodes carry a parent pointer to the fragment
        // root, which dies with this block. Pushed raw, they kept a dangling parent - harmless
        // until something walked it, which the layout dirty-set walk now does: SIGSEGV, found by
        // the reftest for an innerHTML replace.
        for (auto& child : fragment.root().children)
            node->append_child(std::move(child));
    }
    rendering::note_child_list_mutation_at(node);
    if (g_dom_host)
        g_dom_host->register_subtree(*node);
    return JS_UNDEFINED;
}

JSValue js_style_set_property(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    auto* node = node_of(context, this_value);
    if (!node || argc < 2)
        return JS_UNDEFINED;
    const char* property = JS_ToCString(context, argv[0]);
    const char* value = JS_ToCString(context, argv[1]);
    if (property && value)
        apply_style_property(node, property, value);
    if (property)
        JS_FreeCString(context, property);
    if (value)
        JS_FreeCString(context, value);
    return JS_UNDEFINED;
}

JSValue js_style_remove_property(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    auto* node = node_of(context, this_value);
    if (!node || argc < 1)
        return JS_UNDEFINED;
    const char* property = JS_ToCString(context, argv[0]);
    if (!property)
        return JS_NewString(context, "");
    std::string name = property;
    JS_FreeCString(context, property);
    auto declarations = parse_declarations(node);
    std::string removed;
    for (auto it = declarations.begin(); it != declarations.end();) {
        if (it->first == name) {
            removed = it->second;
            it = declarations.erase(it);
        } else {
            ++it;
        }
    }
    write_declarations(node, declarations);
    // CSSStyleDeclaration.removeProperty() returns the value it removed, not undefined.
    return JS_NewString(context, removed.c_str());
}

JSValue js_style_css_text_get(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    auto* node = node_of(context, this_value);
    if (!node)
        return JS_UNDEFINED;
    auto* value = node->attribute("style");
    return JS_NewString(context, value ? value->c_str() : "");
}

JSValue js_style_css_text_set(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    auto* node = node_of(context, this_value);
    if (!node)
        return JS_UNDEFINED;
    const char* raw = argc > 0 ? JS_ToCString(context, argv[0]) : nullptr;
    rendering::set_attribute(*node, "style", raw ? raw : "");
    if (raw)
        JS_FreeCString(context, raw);
    return JS_UNDEFINED;
}

JSValue js_style_get_property_value(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    auto* node = node_of(context, this_value);
    if (!node || argc < 1)
        return JS_UNDEFINED;
    const char* property = JS_ToCString(context, argv[0]);
    std::string name = property ? property : "";
    if (property)
        JS_FreeCString(context, property);
    for (auto const& entry : parse_declarations(node))
        if (entry.first == name)
            return JS_NewString(context, entry.second.c_str());
    return JS_NewString(context, "");
}

JSValue js_style_length_get(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    auto* node = node_of(context, this_value);
    if (!node)
        return JS_UNDEFINED;
    return JS_NewInt32(context, static_cast<int>(parse_declarations(node).size()));
}

// item(index) is the property *name* at that position, per CSS/CSSStyleDeclaration.idl.
JSValue js_style_item(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    auto* node = node_of(context, this_value);
    if (!node || argc < 1)
        return JS_UNDEFINED;
    int index = 0;
    JS_ToInt32(context, &index, argv[0]);
    auto declarations = parse_declarations(node);
    if (index < 0 || static_cast<std::size_t>(index) >= declarations.size())
        return JS_NewString(context, "");
    return JS_NewString(context, declarations[static_cast<std::size_t>(index)].first.c_str());
}

JSValue js_style_get(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    auto* node = node_of(context, this_value);
    JSValue style = JS_NewObjectClass(context, g_dom_node_class);
    JS_SetOpaque(style, node);
    JS_SetPropertyStr(context, style, "setProperty", JS_NewCFunction(context, js_style_set_property, "setProperty", 2));
    JS_SetPropertyStr(context, style, "removeProperty", JS_NewCFunction(context, js_style_remove_property, "removeProperty", 1));
    JS_SetPropertyStr(context, style, "getPropertyValue", JS_NewCFunction(context, js_style_get_property_value, "getPropertyValue", 1));
    JS_SetPropertyStr(context, style, "item", JS_NewCFunction(context, js_style_item, "item", 1));
    // cssText and length are accessors, as in the IDL; a page reads them as properties.
    auto define_on_style = [&](char const* name, JSCFunction* getter, JSCFunction* setter) {
        JSAtom atom = JS_NewAtom(context, name);
        JS_DefinePropertyGetSet(context, style, atom,
            JS_NewCFunction(context, getter, name, 0),
            setter ? JS_NewCFunction(context, setter, name, 1) : JS_UNDEFINED,
            JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
        JS_FreeAtom(context, atom);
    };
    define_on_style("cssText", js_style_css_text_get, js_style_css_text_set);
    define_on_style("length", js_style_length_get, nullptr);
    return style;
}

JSValue js_style_set(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    auto* node = node_of(context, this_value);
    if (!node || argc < 1)
        return JS_UNDEFINED;
    const char* raw = JS_ToCString(context, argv[0]);
    if (raw)
        rendering::set_attribute(*node, "style", raw);
    if (raw)
        JS_FreeCString(context, raw);
    return JS_UNDEFINED;
}

// ---- host ------------------------------------------------------------------

JSValue DomHost::handle_for(rendering::DomNode* node)
{
    auto existing = handle_by_node.find(node);
    if (existing != handle_by_node.end())
        return JS_DupValue(context, existing->second);

    JSValue handle = JS_NewObjectClass(context, g_dom_node_class);
    JS_SetOpaque(handle, node);

    auto* id = node->attribute("id");
    JS_SetPropertyStr(context, handle, "id", JS_NewString(context, id ? id->c_str() : ""));

    JSValue class_list = JS_NewObjectClass(context, g_dom_node_class);
    JS_SetOpaque(class_list, node);
    JS_SetPropertyStr(context, class_list, "add", JS_NewCFunction(context, js_class_add, "add", 1));
    JS_SetPropertyStr(context, class_list, "remove", JS_NewCFunction(context, js_class_remove, "remove", 1));
    JS_SetPropertyStr(context, class_list, "contains", JS_NewCFunction(context, js_class_contains, "contains", 1));
    JS_SetPropertyStr(context, class_list, "toggle", JS_NewCFunction(context, js_class_toggle, "toggle", 1));
    JS_SetPropertyStr(context, handle, "classList", class_list);

    JS_SetPropertyStr(context, handle, "setAttribute", JS_NewCFunction(context, js_set_attribute, "setAttribute", 2));
    JS_SetPropertyStr(context, handle, "getAttribute", JS_NewCFunction(context, js_get_attribute, "getAttribute", 1));
    JS_SetPropertyStr(context, handle, "appendChild", JS_NewCFunction(context, js_append_child, "appendChild", 1));
    JS_SetPropertyStr(context, handle, "remove", JS_NewCFunction(context, js_element_remove, "remove", 0));
    JS_SetPropertyStr(context, handle, "insertBefore", JS_NewCFunction(context, js_element_insert_before, "insertBefore", 2));
    JS_SetPropertyStr(context, handle, "addEventListener", JS_NewCFunction(context, js_element_add_event_listener, "addEventListener", 3));
    JS_SetPropertyStr(context, handle, "removeEventListener", JS_NewCFunction(context, js_element_remove_event_listener, "removeEventListener", 3));

    auto define_accessor = [&](char const* name, JSCFunction* getter, JSCFunction* setter) {
        JSAtom atom = JS_NewAtom(context, name);
        JS_DefinePropertyGetSet(context, handle, atom,
            JS_NewCFunction(context, getter, name, 0),
            JS_NewCFunction(context, setter, name, 1),
            JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
        JS_FreeAtom(context, atom);
    };
    define_accessor("textContent", js_text_content_get, js_text_content_set);
    define_accessor("innerHTML", js_inner_html_get, js_inner_html_set);
    define_accessor("className", js_class_name_get, js_class_name_set);
    JS_SetPropertyStr(context, handle, "getBoundingClientRect",
        JS_NewCFunction(context, js_get_bounding_client_rect, "getBoundingClientRect", 0));
    JS_SetPropertyStr(context, handle, "focus", JS_NewCFunction(context, js_focus, "focus", 0));
    JS_SetPropertyStr(context, handle, "blur", JS_NewCFunction(context, js_blur, "blur", 0));
    define_accessor("style", js_style_get, js_style_set);

    auto define_getter = [&](char const* name, JSCFunction* getter) {
        JSAtom atom = JS_NewAtom(context, name);
        JS_DefinePropertyGetSet(context, handle, atom,
            JS_NewCFunction(context, getter, name, 0), JS_UNDEFINED, JS_PROP_CONFIGURABLE);
        JS_FreeAtom(context, atom);
    };
    define_getter("children", js_children_get);

    auto define_accessor_pair = [&](char const* name, JSCFunction* getter, JSCFunction* setter) {
        JSAtom atom = JS_NewAtom(context, name);
        JS_DefinePropertyGetSet(context, handle, atom,
            JS_NewCFunction(context, getter, name, 0),
            JS_NewCFunction(context, setter, name, 1),
            JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
        JS_FreeAtom(context, atom);
    };
    define_accessor_pair("value", js_value_get, js_value_set);
    define_accessor_pair("checked", js_checked_get, js_checked_set);

    JS_SetPropertyStr(context, handle, "submit", JS_NewCFunction(context, js_form_submit, "submit", 0));
    define_getter("parentNode", js_parent_node_get);

    JSAtom tag_atom = JS_NewAtom(context, "tagName");
    JS_DefinePropertyGetSet(context, handle, tag_atom,
        JS_NewCFunction(context, js_element_tag_name, "get tagName", 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_FreeAtom(context, tag_atom);

    JSAtom dataset_atom = JS_NewAtom(context, "dataset");
    JS_DefinePropertyGetSet(context, handle, dataset_atom,
        JS_NewCFunction(context, js_element_dataset, "get dataset", 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_FreeAtom(context, dataset_atom);

    handle_by_node[node] = JS_DupValue(context, handle);
    if (id)
        handle_by_id[*id] = JS_DupValue(context, handle);
    return handle;
}

void DomHost::collect(rendering::DomNode& node)
{
    for (auto& child : node.children) {
        parent_of[child.get()] = &node;
        collect(*child);
    }
}

void DomHost::register_subtree(rendering::DomNode& node)
{
    // No mutation is noted here: this walks the tree to hand out JS handles, and the caller
    // records which child list changed.
    for (auto& child : node.children) {
        parent_of[child.get()] = &node;
        if (child->type == rendering::DomNodeType::Element) {
            JSValue handle = handle_for(child.get());
            JS_FreeValue(context, handle);
        }
        register_subtree(*child);
    }
}

void DomHost::purge_subtree(rendering::DomNode& node)
{
    // As in register_subtree: handles only. The caller records the child-list change.
    for (auto& child : node.children) {
        auto it = handle_by_node.find(child.get());
        if (it != handle_by_node.end()) {
            for (auto id_it = handle_by_id.begin(); id_it != handle_by_id.end();) {
                if (JS_VALUE_GET_PTR(id_it->second) == JS_VALUE_GET_PTR(it->second)) {
                    JS_FreeValue(context, id_it->second);
                    id_it = handle_by_id.erase(id_it);
                } else {
                    ++id_it;
                }
            }
            JS_FreeValue(context, it->second);
            handle_by_node.erase(it);
        }
        parent_of.erase(child.get());
        purge_subtree(*child);
    }
}

void DomHost::adopt(rendering::DomNode* parent, rendering::DomNode* child)
{
    std::unique_ptr<rendering::DomNode> moved;
    auto pending_it = pending.find(child);
    if (pending_it != pending.end()) {
        moved = std::move(pending_it->second);
        pending.erase(pending_it);
    } else {
        auto parent_it = parent_of.find(child);
        if (parent_it != parent_of.end()) {
            moved = detach_child(*parent_it->second, child);
            // The sequence it left changed too, and so did every sibling's position in it.
            rendering::note_child_list_mutation_at(parent_it->second);
        }
        if (!moved)
            return;
    }
    parent->append_child(std::move(moved));
    rendering::note_child_list_mutation_at(parent);
    parent_of[child] = parent;
}

} // namespace

// Builds a JavaScript handle for every element in the tree.
static void build_handles(rendering::DomNode& node, DomHost& dom)
{
    if (node.type == rendering::DomNodeType::Element) {
        JSValue handle = dom.handle_for(&node);
        JS_FreeValue(dom.context, handle);
    }
    for (auto& child : node.children)
        build_handles(*child, dom);
}

// Handles are linked lazily: children and parentNode are getters that read the
// live tree, so insertBefore/remove are reflected immediately.

// Fires an event at the node and up its ancestor chain.
static void dispatch_bubbling(aetheris::js::Interpreter& js, DomHost& dom, rendering::DomNode* node, std::string const& type)
{
    auto handle_it = dom.handle_by_node.find(node);
    if (handle_it == dom.handle_by_node.end())
        return;
    js.dispatch_event_to(JS_DupValue(dom.context, handle_it->second), type, [&dom](JSValue current) -> JSValue {
        auto* current_node = static_cast<rendering::DomNode*>(JS_GetOpaque(current, g_dom_node_class));
        JS_FreeValue(dom.context, current);
        if (!current_node)
            return JS_UNDEFINED;
        auto parent = dom.parent_of.find(current_node);
        if (parent == dom.parent_of.end())
            return JS_UNDEFINED;
        auto parent_handle = dom.handle_by_node.find(parent->second);
        if (parent_handle == dom.handle_by_node.end())
            return JS_UNDEFINED;
        return JS_DupValue(dom.context, parent_handle->second);
    });
}

struct SimpleSelector {
    std::string tag;
    std::string id;
    std::vector<std::string> classes;
};

static bool parse_simple_selector(std::string const& text, SimpleSelector& out)
{
    std::size_t i = 0;
    while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '-' || text[i] == '*')) {
        if (text[i] != '*')
            out.tag += text[i];
        ++i;
    }
    while (i < text.size()) {
        if (text[i] == '#') {
            ++i;
            while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '-'))
                out.id += text[i++];
        } else if (text[i] == '.') {
            ++i;
            std::string name;
            while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '-'))
                name += text[i++];
            if (name.empty())
                return false;
            out.classes.push_back(name);
        } else {
            return false; // unsupported syntax (attributes, pseudo-classes, ...)
        }
    }
    return true;
}

static bool element_matches(rendering::DomNode const& node, SimpleSelector const& selector)
{
    if (node.type != rendering::DomNodeType::Element)
        return false;
    if (!selector.tag.empty() && node.name != selector.tag)
        return false;
    if (!selector.id.empty()) {
        auto* id = node.attribute("id");
        if (!id || *id != selector.id)
            return false;
    }
    for (auto const& wanted : selector.classes) {
        auto* classes = node.attribute("class");
        if (!classes)
            return false;
        bool found = false;
        std::string current;
        std::string text = *classes;
        text.push_back(' ');
        for (char c : text) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (current == wanted)
                    found = true;
                current.clear();
            } else {
                current += c;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

static bool matches_chain(rendering::DomNode const& node, std::vector<SimpleSelector> const& chain, std::size_t index, DomHost& dom)
{
    if (!element_matches(node, chain[index]))
        return false;
    if (index == 0)
        return true;
    auto parent = dom.parent_of.find(const_cast<rendering::DomNode*>(&node));
    for (auto* ancestor = parent != dom.parent_of.end() ? parent->second : nullptr; ancestor;) {
        if (matches_chain(*ancestor, chain, index - 1, dom))
            return true;
        auto up = dom.parent_of.find(ancestor);
        ancestor = up != dom.parent_of.end() ? up->second : nullptr;
    }
    return false;
}

// Splits "div .a, #b" into chains of simple selectors.
static bool parse_selector(std::string const& selector, std::vector<std::vector<SimpleSelector>>& chains)
{
    std::size_t start = 0;
    while (start <= selector.size()) {
        auto comma = selector.find(',', start);
        std::string group = selector.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        std::vector<SimpleSelector> chain;
        std::size_t position = 0;
        while (position < group.size()) {
            while (position < group.size() && std::isspace(static_cast<unsigned char>(group[position])))
                ++position;
            std::size_t token_start = position;
            while (position < group.size() && !std::isspace(static_cast<unsigned char>(group[position])))
                ++position;
            if (position == token_start)
                break;
            SimpleSelector simple;
            if (!parse_simple_selector(group.substr(token_start, position - token_start), simple))
                return false;
            chain.push_back(simple);
        }
        if (chain.empty())
            return false;
        chains.push_back(chain);
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return !chains.empty();
}

static void collect_selector_matches(rendering::DomNode& node, DomHost& dom, std::vector<std::vector<SimpleSelector>> const& chains, std::vector<rendering::DomNode*>& out)
{
    if (node.type == rendering::DomNodeType::Element) {
        for (auto const& chain : chains) {
            if (matches_chain(node, chain, chain.size() - 1, dom)) {
                out.push_back(&node);
                break;
            }
        }
    }
    for (auto& child : node.children)
        collect_selector_matches(*child, dom, chains, out);
}

// Collects the bodies of inline <script> elements.
// Reads the first <meta http-equiv="Content-Security-Policy"> in the document.
static std::string content_security_policy(rendering::DomNode const& root)
{
    for (auto const& child : root.children) {
        if (child->type == rendering::DomNodeType::Element && child->name == "meta") {
            auto* http_equiv = child->attribute("http-equiv");
            auto* content = child->attribute("content");
            if (!http_equiv || !content)
                continue;
            std::string name = *http_equiv;
            for (auto& c : name)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (name == "content-security-policy")
                return *content;
        }
        auto nested = content_security_policy(*child);
        if (!nested.empty())
            return nested;
    }
    return {};
}

// The inline-script question moved to ContentSecurityPolicy::allows_inline_script() when the fetch directives
// landed (row 5a): one policy object now answers both "may this script run" and "may this URL be fetched".

static std::vector<std::string> extract_scripts(std::string const& html)
{
    std::vector<std::string> scripts;
    std::size_t position = 0;
    while (true) {
        auto open = html.find("<script", position);
        if (open == std::string::npos)
            break;
        auto tag_end = html.find('>', open);
        if (tag_end == std::string::npos)
            break;
        auto close = html.find("</script", tag_end);
        if (close == std::string::npos)
            break;
        scripts.push_back(html.substr(tag_end + 1, close - tag_end - 1));
        position = close + 8;
    }
    return scripts;
}

// Collects the bodies of inline <style> elements.
static std::vector<std::string> extract_styles(std::string const& html)
{
    std::vector<std::string> styles;
    std::size_t position = 0;
    while (true) {
        auto open = html.find("<style", position);
        if (open == std::string::npos)
            break;
        auto tag_end = html.find('>', open);
        if (tag_end == std::string::npos)
            break;
        auto close = html.find("</style", tag_end);
        if (close == std::string::npos)
            break;
        styles.push_back(html.substr(tag_end + 1, close - tag_end - 1));
        position = close + 7;
    }
    return styles;
}

// Headless script evaluation: run a snippet and print console output.
static int run_eval(std::string const& source)
{
    aetheris::js::Interpreter js;
    js.print_handler = [](std::string const& line) { std::cout << line << std::endl; };
    js.write_handler = [](std::string const& text) { std::cout << text; };
    js.run(source);
    if (js.has_error()) {
        std::cerr << "JavaScript error: " << js.error() << std::endl;
        return 1;
    }
    return 0;
}

// Headless render mode: fetch a URL, run the rendering pipeline and write the
// resulting surface to a PPM file. Exercises the full fetch -> parse -> style
// -> layout -> paint path without a display server.
// A compact browser chrome: one 34px strip holding tabs, navigation and the
// address bar, so the page gets the rest of the window.
static constexpr int kChromeHeight = 34;

static std::string document_title(rendering::DomNode const& node)
{
    if (node.type == rendering::DomNodeType::Element && node.name == "title")
        return node_text(node);
    for (auto const& child : node.children) {
        auto found = document_title(*child);
        if (!found.empty())
            return found;
    }
    return {};
}

static std::string chrome_document(ui::BrowserSession const& session, std::string const& address)
{
    auto escape = [](std::string const& value) {
        std::string out;
        for (char c : value) {
            if (c == '&') out += "&amp;";
            else if (c == '<') out += "&lt;";
            else if (c == '>') out += "&gt;";
            else out += c;
        }
        return out;
    };
    auto truncate = [](std::string text, std::size_t limit) {
        if (text.size() > limit)
            text = text.substr(0, limit - 1) + "\u2026";
        return text;
    };

    // One layout for drawing and for hit-testing: the tab boxes painted here are the ones a click is matched
    // against (row 4.1).
    auto const layout = ui::ChromeLayout::compute(session.tab_count(), session.active_index(),
        static_cast<int>(1024));

    auto box = [](int left, int top, int width, int height, std::string const& background,
                   std::string const& radius) {
        return "position:absolute;left:" + std::to_string(left) + "px;top:" + std::to_string(top)
            + "px;width:" + std::to_string(width) + "px;height:" + std::to_string(height)
            + "px;background:" + background + ";border-radius:" + radius + ";overflow:hidden";
    };
    auto label = [](int left, int top, std::string const& text, std::string const& colour, int size) {
        return "<div style='position:absolute;left:" + std::to_string(left) + "px;top:"
            + std::to_string(top) + "px;color:" + colour + ";font-size:" + std::to_string(size)
            + "px;white-space:nowrap'>" + text + "</div>";
    };

    std::string html = "<html><body style='margin:0;background:#202124'>";
    for (auto const& tab_box : layout.tabs) {
        auto const* tab = session.tab(tab_box.index);
        bool const active = tab_box.index == session.active_index();
        // The active tab is lighter and taller-looking; the others recede. That is the only difference a strip needs.
        html += "<div style='" + box(tab_box.left, active ? 3 : 5, tab_box.width, active ? 28 : 24,
            active ? "#3c4043" : "#2a2b2e", "6px") + "'></div>";
        auto const title = tab ? truncate(tab->title.empty() ? tab->url : tab->title,
                               static_cast<std::size_t>(std::max(6, (tab_box.width - 44) / 7)))
                               : std::string("New Tab");
        html += label(tab_box.left + 10, active ? 9 : 10, escape(title), active ? "#e8eaed" : "#9aa0a6", 12);
        // The close button, at the position the hit test will use.
        html += label(layout.close_left(tab_box) - 2, active ? 8 : 9, "\u00d7", active ? "#e8eaed" : "#9aa0a6", 13);
    }
    html += label(layout.new_tab_left + 4, 8, "+", "#9aa0a6", 15);
    html += label(layout.back_left + 6, 8, "\u2190", "#9aa0a6", 14);
    html += label(layout.forward_left + 6, 8, "\u2192", "#9aa0a6", 14);
    html += label(layout.reload_left + 6, 8, "\u21bb", "#9aa0a6", 14);
    html += "<div style='" + box(layout.address_left, 5, layout.address_width, 24, "#35363a", "12px") + "'></div>";
    html += label(layout.address_left + 10, 10, escape(address), "#e8eaed", 12);
    html += label(1024 - 30, 8, "\u22ee", "#9aa0a6", 14);
    html += "</body></html>";
    return html;
}

// Runs the browser in a window: render, present, pump events. Without a display
// backend this still works headlessly for a fixed number of frames, which is
// what CI and the tests use.
// Collects a page's CSS: linked stylesheets, then inline <style> blocks. Both
// the CLI render path and the browser window use this — the window used to pass
// an empty stylesheet, so every page rendered unstyled.
struct PageStyle {
    std::string css;
    std::size_t linked_stylesheets { 0 };
};

static PageStyle collect_page_style(std::string const& body, std::string const& url, SecureFetcher& fetcher)
{
    PageStyle style;
    rendering::ResourceCache scratch;
    rendering::ResourceLoader loader(scratch);
    rendering::Url base = rendering::Url::parse(url);
    rendering::ResourceData document_resource {
        rendering::ResourceType::Document,
        url,
        std::vector<unsigned char>(body.begin(), body.end()),
    };

    auto trace = [](char const* what) {
        if (std::getenv("AETHERIS_TRACE_DOC")) std::fprintf(stderr, "STYLE %s\n", what);
    };
    trace("discover begin");
    auto dependencies = loader.discover_dependencies(document_resource, base);
    trace("discover done");
    for (auto const& dependency : dependencies) {
        std::string path = dependency.path();
        bool is_css = path.size() >= 4 && path.compare(path.size() - 4, 4, ".css") == 0;
        if (!is_css)
            continue;
        auto fetched = fetcher.fetch(rendering::ResourceRequestContext::Style, dependency);
        if (!fetched.succeeded())
            continue;
        ++style.linked_stylesheets;
        if (!style.css.empty())
            style.css.push_back('\n');
        style.css.append(fetched.resource->bytes.begin(), fetched.resource->bytes.end());
    }

    trace("extract begin");
    auto blocks = extract_styles(body);
    trace("extract done");
    for (auto const& block : blocks) {
        if (!style.css.empty())
            style.css.push_back('\n');
        style.css += block;
    }
    return style;
}

// Defined below, after the DOM host. Declared here so the window loop can run
// page scripts through the same code path as --render.
static rendering::RenderDocument create_scripted_document(
    std::string const& html, std::string const& css, std::string const& url,
    rendering::ResourceCache* resources,
    aetheris::js::Interpreter& js, DomHost& dom,
    std::shared_ptr<std::array<double, 4>> viewport_state, double timer_budget_ms,
    SecureFetcher* fetcher);


static rendering::DomNode* node_at(rendering::RenderDocument& rendered, DomHost& dom,
    rendering::Viewport const& viewport, float x, float y);

// Attributes the engine reads outside the cascade: changing one can change layout or
// paint even when no selector mentions it.
static bool attribute_is_rendering_relevant(std::string const& name)
{
    static char const* const relevant[] = { "style", "class", "id", "src", "href", "width",
        "height", "type", "value", "checked", "disabled", "hidden", "colspan", "rowspan", "alt" };
    for (auto const* candidate : relevant)
        if (name == candidate)
            return true;
    return false;
}

// Upstream invalidates only what a rule can observe. Applied to attribute writes: a
// `data-` style attribute that no selector mentions cannot change style, layout or
// paint, so the rebuild is wasted work.
static bool attribute_mutation_needs_relayout(rendering::RenderDocument const& page)
{
    auto const& name = rendering::last_attribute_mutation();
    if (name.empty())
        return true;
    if (attribute_is_rendering_relevant(name))
        return true;
    return page.stylesheet().uses_attribute(name);
}

// Sequentially focusable, as upstream's Element::is_focusable() defines it: an
// element whose tabindex parses as an integer. Upstream also requires the
// focusable-area rendering rules, and links and form controls have their own
// clauses; neither exists here yet.
static bool parse_integer(std::string const& text, int& out)
{
    if (text.empty())
        return false;
    std::size_t index = 0;
    bool negative = false;
    if (text[0] == '+' || text[0] == '-') {
        negative = text[0] == '-';
        index = 1;
        if (text.size() == 1)
            return false;
    }
    long value = 0;
    for (; index < text.size(); ++index) {
        if (text[index] < '0' || text[index] > '9')
            return false;
        value = value * 10 + (text[index] - '0');
        if (value > 1000000)
            return false;
    }
    out = static_cast<int>(negative ? -value : value);
    return true;
}

// Upstream's clauses, by tag name: Element::is_focusable() is a tabindex that
// parses, HTMLAnchorElement adds "has an href", and form controls override with
// their own rule (not disabled). The rendering requirement
// (meets_focusable_area_rendering_requirements) is not checked here - this layer
// has no per-node computed style to ask.
static bool is_focusable(rendering::DomNode const& node)
{
    auto const* tabindex = node.attribute("tabindex");
    int value = 0;
    if (tabindex && parse_integer(*tabindex, value))
        return true;

    if (node.name == "a" && node.attribute("href") != nullptr)
        return true;

    bool const control = node.name == "input" || node.name == "button"
        || node.name == "select" || node.name == "textarea";
    return control && node.attribute("disabled") == nullptr;
}

static void collect_focusable(rendering::DomNode const& node, std::vector<rendering::DomNode*>& out)
{
    if (is_focusable(node))
        out.push_back(const_cast<rendering::DomNode*>(&node));
    for (auto const& child : node.children)
        collect_focusable(*child, out);
}

static rendering::DomNode* next_focusable(rendering::RenderDocument& rendered, rendering::DomNode* current)
{
    std::vector<rendering::DomNode*> focusable;
    collect_focusable(rendered.document().root(), focusable);
    if (focusable.empty())
        return nullptr;
    if (!current)
        return focusable.front();
    for (std::size_t index = 0; index < focusable.size(); ++index)
        if (focusable[index] == current)
            return focusable[(index + 1) % focusable.size()];
    return focusable.front();
}

// Moves focus, in the order Ladybird's Document::set_focused_area uses: the old
// element loses focus, style is re-resolved for the change, then the new element
// receives focus. Firing `blur` before the state changes and `focus` after is
// what lets a handler see the right document.activeElement.
static bool set_focus(rendering::RenderDocument& rendered, aetheris::js::Interpreter& js,
    DomHost& dom, rendering::VisualInteractionState& state, rendering::DomNode* node,
    float viewport_width)
{
    if (state.focused == node)
        return false;

    auto* previous = const_cast<rendering::DomNode*>(state.focused);
    if (previous && dom.handle_by_node.find(previous) != dom.handle_by_node.end())
        dispatch_bubbling(js, dom, previous, "blur");

    if (!state.set_focused(node))
        return false;
    rendered.apply_visual_state(state, viewport_width);

    if (node && dom.handle_by_node.find(node) != dom.handle_by_node.end())
        dispatch_bubbling(js, dom, node, "focus");
    return true;
}

// A key goes to the focused element when there is one, and bubbles from there;
// with nothing focused it goes to the document, which is what a page expects.
static void dispatch_key_to_focus(aetheris::js::Interpreter& js, DomHost& dom,
    rendering::DomNode* focused, std::string const& key)
{
    if (focused && dom.handle_by_node.find(focused) != dom.handle_by_node.end()) {
        js.set_event_key(key);
        // The element first, then the document and window: a key event bubbles
        // from the focused element all the way up, which is what Ladybird's
        // EventHandler does and what a page listening on document expects.
        dispatch_bubbling(js, dom, focused, "keydown");
        js.dispatch_event("keydown");
        // The key coming back up takes the same path, as upstream's
        // EventHandler::handle_keyup does through fire_keyboard_event.
        dispatch_bubbling(js, dom, focused, "keyup");
        js.dispatch_event("keyup");
        js.set_event_key({});
        return;
    }
    // Set the key around both events: dispatch_key() clears it after its keydown,
    // which is why the first attempt logged `keyup undefined`.
    js.set_event_key(key);
    js.dispatch_event("keydown");
    js.dispatch_event("keyup");
    js.set_event_key({});
}

// Applies a pressed element to the cascade, so `:active` styling shows while the
// button is down. Returns true when the state changed and a frame is needed.
static bool apply_active_visual_state(rendering::RenderDocument& rendered,
    rendering::VisualInteractionState& state, rendering::DomNode* node, float viewport_width)
{
    if (!state.set_active(node))
        return false;
    return rendered.apply_visual_state(state, viewport_width);
}

// Applies a hover change to the cascade. Returns true when the page needs a new
// frame: a :hover rule may have matched, and the only way to know is to rebuild.
static bool apply_hover_visual_state(rendering::RenderDocument& rendered,
    rendering::VisualInteractionState& state, rendering::DomNode* node, float viewport_width)
{
    if (!state.set_hovered(node))
        return false;
    return rendered.apply_visual_state(state, viewport_width);
}

// Moves the pointer to a point in the content area: the element losing the
// pointer gets `mouseout`, the one gaining it gets `mouseover`. Returns true when
// the element under the pointer changed, which is the only time a repaint is
// worth doing.
static bool hover_at(rendering::RenderDocument& rendered, aetheris::js::Interpreter& js,
    DomHost& dom, rendering::Viewport const& viewport, float x, float y, rendering::DomNode*& hovered)
{
    if (js.has_error())
        return false;
    auto* node = node_at(rendered, dom, viewport, x, y);
    if (node == hovered)
        return false;
    if (hovered)
        dispatch_bubbling(js, dom, hovered, "mouseout");
    hovered = node;
    if (hovered)
        dispatch_bubbling(js, dom, hovered, "mouseover");
    return true;
}

// Fires a click at a point in the content area on the element there, then
// re-lays out so whatever the handler changed is visible. Shared by the window's
// own clicks and by --click, so both take exactly the same path.
// The element at a point, or null. Text nodes and anonymous boxes have no
// JavaScript handle, so the hit walks up to the nearest ancestor that does.
static rendering::DomNode* node_at(rendering::RenderDocument& rendered, DomHost& dom,
    rendering::Viewport const& viewport, float x, float y)
{
    rendering::DocumentInteraction interaction;
    auto hit = interaction.hit_test(rendered, viewport, x, y);
    auto* node = hit.dom_node ? const_cast<rendering::DomNode*>(hit.dom_node) : nullptr;
    while (node && dom.handle_by_node.find(node) == dom.handle_by_node.end()) {
        auto parent = dom.parent_of.find(node);
        node = parent != dom.parent_of.end() ? parent->second : nullptr;
    }
    return node;
}

static bool dispatch_click_at(rendering::RenderDocument& rendered, aetheris::js::Interpreter& js,
    DomHost& dom, rendering::Viewport const& viewport, float x, float y)
{
    if (js.has_error())
        return false;

    auto* node = node_at(rendered, dom, viewport, x, y);
    if (!node) {
        std::cerr << "  [js] no element handle at the click point" << std::endl;
        return false;
    }
    auto const before = rendering::dom_mutation_serial();
    dispatch_bubbling(js, dom, node, "click");
    // ...and then the document and window, which are not in the DOM ancestor
    // chain. Upstream bubbles a click from the target all the way up, so a page
    // listening on document must hear it too.
    js.dispatch_event("click");
    // A click that changed nothing must not reflow. relayout() with no mutation on record is a
    // structural change to it - it clears the style cache and lays the page out from scratch -
    // so clicking a static page used to be the most expensive thing the window could do:
    // 407 resolves and 405 nodes laid out, for a handler that only logged.
    if (rendering::dom_mutation_serial() != before && attribute_mutation_needs_relayout(rendered))
        rendered.relayout();
    return true;
}

static int run_window(std::string const& url, int frames, std::string const& frame_directory,
    int click_x = -1, int click_y = -1, std::string const& key_to_press = {},
    int move_x = -1, int move_y = -1, int press_x = -1, int press_y = -1,
    int focus_key_x = -1, int focus_key_y = -1)
{
    constexpr int kWidth = 1024;
    constexpr int kHeight = 768;
    int content_height = kHeight - kChromeHeight;

    ui::BrowserSession session;
    if (!session.open(url)) {
        std::cerr << "Not a loadable URL: " << url << std::endl;
        return 1;
    }

    // Scroll state for the page area. Declared here, above the history hooks, because a traversal has to record
    // where the page was scrolled before it moves (row 1g4c).
    rendering::Viewport viewport(static_cast<float>(kWidth), static_cast<float>(content_height));

    // `location` asks these two questions and nothing else.
    aetheris::js::set_navigation_hooks(
        [] { return g_current_url; },
        [](std::string url) { g_current_url = std::move(url); },
        [](std::string target, bool reload, bool replace) {
            g_navigation_request = reload || target.empty() ? g_current_url : resolve_url(g_current_url, target);
            g_navigation_pending = true;
            g_navigation_reload = reload;
            g_navigation_replace = replace;
        },
        [&session] { return session.history_length(); },
        // A history move changes the tab's URL; the page then has to be reloaded, which is the same
        // request the loop already performs for a scripted navigation. Without this the URL moved and
        // nothing re-rendered - the page stayed on the old document.
        [&session, &viewport] {
            session.record_scroll(viewport.scroll_x(), viewport.scroll_y());
            if (!session.history_back())
                return false;
            g_navigation_request = session.current_url();
            g_navigation_pending = true;
            g_navigation_reload = true;
            return true;
        },
        [&session, &viewport] {
            session.record_scroll(viewport.scroll_x(), viewport.scroll_y());
            if (!session.history_forward())
                return false;
            g_navigation_request = session.current_url();
            g_navigation_pending = true;
            g_navigation_reload = true;
            return true;
        },
        // go(0) is a reload, which is the same request with the reload flag set.
        [&session, &viewport](int delta) {
            session.record_scroll(viewport.scroll_x(), viewport.scroll_y());
            if (delta == 0) {
                g_navigation_request = session.current_url();
                g_navigation_pending = true;
                g_navigation_reload = true;
                return true;
            }
            if (!session.history_go(delta))
                return false;
            g_navigation_request = session.current_url();
            g_navigation_pending = true;
            g_navigation_reload = true;
            return true;
        });


    // `history.state`, `pushState`/`replaceState` and `scrollRestoration` (rows 1g4b, 1g4c). The session owns the
    // state text; the binding serialises to JSON because this engine has no structured clone.
    aetheris::js::set_history_state_hooks(
        [&session](std::string const& url, std::string state, bool replace) {
            return session.push_state(url, std::move(state), replace);
        },
        [&session] { return session.current_state(); },
        [&session] { return session.scroll_restoration() == ui::ScrollRestoration::Manual ? 1 : 0; },
        [&session](int mode) {
            session.set_scroll_restoration(mode == 1 ? ui::ScrollRestoration::Manual : ui::ScrollRestoration::Auto);
        });

    // The loaded page is kept between frames. Before this, every repaint
    // refetched the document over the network, which made a scroll a download.
    // The page's JavaScript and DOM host live as long as the window: the host
    // owns the nodes scripts create, so it must outlive the document, and the
    // interpreter must outlive the host. Declaring them here, before the cached
    // page, gives exactly that destruction order.
    aetheris::js::Interpreter page_js;
    DomHost page_dom;
    auto page_viewport_state = std::make_shared<std::array<double, 4>>(std::array<double, 4> { 0.0, 0.0, 0.0, 768.0 });
    std::optional<rendering::RenderDocument> cached_page;
    bool page_dirty = true;
    float document_height = static_cast<float>(content_height);
    bool scroll_bar_visible = false;

    // Loads the active tab and composites a frame. Returns false when the page
    // could not be fetched, which the chrome then shows.
    auto render_frame = [&](rendering::SoftwareSurface& window) {
        if (page_dirty) {
            if (network::Socket::initialize() != network::SocketError::None) {
                std::cerr << "Failed to initialize network subsystem" << std::endl;
                return false;
            }
            rendering::Url document_url = rendering::Url::parse(session.current_url());
            SecureFetcher fetcher(document_url);
            if (std::getenv("AETHERIS_TRACE_NAV"))
                std::fprintf(stderr, "NAV fetch [%s]\n", session.current_url().c_str());
            auto loaded = fetcher.fetch(rendering::ResourceRequestContext::Document, document_url);
            if (std::getenv("AETHERIS_TRACE_DOC")) std::fprintf(stderr, "DOC fetched ok=%d\n", loaded.succeeded() ? 1 : 0);
            network::Socket::cleanup();
            if (std::getenv("AETHERIS_TRACE_DOC")) std::fprintf(stderr, "DOC socket cleanup done\n");
            if (!loaded.succeeded()) {
                session.set_error(loaded.message);
                page_dirty = false;
                cached_page.reset();
                return false;
            }
            std::string body(loaded.resource->bytes.begin(), loaded.resource->bytes.end());
            // The URL and the storage origin are set BEFORE the page is built: its scripts run inside
            // create_scripted_document(), so `location.href` and `localStorage` have to be right by then.
            g_current_url = session.current_url();
            // One definition of "origin" for the whole engine: SecurityPolicy's, which lowercases scheme and host and
            // drops the path. Hand-rolling it here meant two spellings of one host could get two storage buckets.
            page_js.set_storage_origin(rendering::SecurityPolicy::origin(rendering::Url::parse(g_current_url)));
            // A navigation is a new realm: the previous document's globals, listeners and timers go, and the DOM
            // host's handles go with the context that owned them (row 1g2). Storage survives - it is keyed by origin.
            page_dom.release();
            page_js.reset_realm();
            page_dom.initialize(page_js.context());
            if (std::getenv("AETHERIS_TRACE_DOC")) std::fprintf(stderr, "DOC style begin\n");
            auto style = collect_page_style(body, session.current_url(), fetcher);
            if (std::getenv("AETHERIS_TRACE_DOC")) std::fprintf(stderr, "DOC style done\n");
            // Scripts run here, so a page that builds its content in JavaScript
            // renders in the window instead of coming up blank.
            cached_page.emplace(create_scripted_document(body, style.css, session.current_url(), nullptr,
                page_js, page_dom, page_viewport_state, 0.0, &fetcher));
            g_current_page = &*cached_page;
session.set_page_title(document_title(cached_page->document().root()));
            session.set_loading(false);

            // The viewport can only scroll once it knows how tall the document
            // is; without this the clamp pins scroll to zero and a long page is
            // unreachable no matter what the wheel does.
            document_height = std::max(cached_page->document_rect().height, static_cast<float>(content_height));
            viewport.set_size(static_cast<float>(kWidth), static_cast<float>(content_height));
            viewport.set_document_size(static_cast<float>(kWidth), document_height);
            // `history.scrollRestoration`: a traversal puts the scroll back where the entry left it unless the
            // page asked to do it itself. A fresh navigation has nothing recorded, so this lands on (0, 0) - which
            // is what it did before the mode existed (row 1g4c).
            float restore_x = 0;
            float restore_y = 0;
            if (session.take_scroll_restore(restore_x, restore_y))
                viewport.scroll_to(restore_x, restore_y);
            else
                viewport.scroll_to(0, 0);
            page_dirty = false;
        }
        if (!cached_page)
            return false;

        rendering::PaintOffset offset { viewport.scroll_x(), viewport.scroll_y() };
        auto page_surface = cached_page->render_to_surface(kWidth, content_height, rendering::Color { 255, 255, 255, 255 }, offset);

        // The scrollbar is painted into the content area, not the window, so its
        // geometry stays in the same coordinate space as the page it describes.
        auto bar = rendering::ScrollBar::geometry(static_cast<float>(kWidth), static_cast<float>(content_height), document_height, offset.y);
        scroll_bar_visible = bar.visible;
        rendering::ScrollBar::paint(page_surface, bar,
            rendering::Color { 0, 0, 0, 24 },
            rendering::Color { 80, 80, 88, 200 });

        window.blit(page_surface, 0, kChromeHeight);
        return true;
    };

    auto compose_chrome = [&](rendering::SoftwareSurface& window) {
        std::string status = session.failed() ? session.error() : session.current_url();
        auto chrome = rendering::RenderDocument::create(
            chrome_document(session, status), "", static_cast<float>(kWidth));
        auto chrome_surface = chrome.render_to_surface(kWidth, kChromeHeight, rendering::Color { 32, 33, 36, 255 });
        window.blit(chrome_surface, 0, 0);
    };

    auto host = ui::make_window_host(frames, frame_directory);
    if (!host->open(kWidth, kHeight, session.display_title())) {
        std::cerr << "Could not open a window" << std::endl;
        return 1;
    }

    // A panel replaces the page area until it is closed: history (Ctrl+H) or bookmarks (Ctrl+B) (row 4.2).
    std::optional<ui::LibraryPage::Mode> panel;

    rendering::SoftwareSurface window(kWidth, kHeight, rendering::Color { 255, 255, 255, 255 });
    bool need_render = true;
    int handled = 0;
    int presented = 0;
    bool done = false;

    bool pending_click_done = false;
    bool pending_key_done = false;
    bool pending_move_done = false;
    bool pending_press_done = false;
    bool pending_focus_key_done = false;
    rendering::DomNode* hovered = nullptr;
    rendering::DomNode* focused = nullptr;
    // The cascade already understands :hover (style.cpp) and apply_visual_state
    // rebuilds layout when it changes; nothing was feeding it.
    rendering::VisualInteractionState visual_state;
    // document.activeElement, answered from the interaction state the way
    // Document::active_element() is upstream. Null when nothing is focused.
    page_js.active_element = [&page_dom, &focused, &cached_page]() -> JSValue {
        auto handle_for = [&page_dom](rendering::DomNode* node) -> JSValue {
            if (node && page_dom.handle_by_node.find(node) != page_dom.handle_by_node.end())
                return JS_DupValue(page_dom.context, page_dom.handle_by_node[node]);
            return JS_UNDEFINED;
        };
        if (auto value = handle_for(focused); !JS_IsUndefined(value))
            return value;
        // Upstream's Document::active_element() falls back to body() when nothing
        // is focused, so a page can assume activeElement is always an element.
        if (cached_page) {
            rendering::DomNode const* body = nullptr;
            std::function<void(rendering::DomNode const&)> find_body = [&](rendering::DomNode const& node) {
                if (body || node.name != "body")
                    for (auto const& child : node.children)
                        find_body(*child);
                if (!body && node.name == "body")
                    body = &node;
            };
            find_body(cached_page->document().root());
            if (auto value = handle_for(const_cast<rendering::DomNode*>(body)); !JS_IsUndefined(value))
                return value;
        }
        return JS_UNDEFINED;
    };
    std::size_t relayouts = 0;
    ui::WindowEvent event;
    ui::InputRouter router(kChromeHeight);
    router.set_address_text(session.current_url());

    int trace_frame = 0;
    while (host->is_open() && !done) {
        if (std::getenv("AETHERIS_TRACE_NAV"))
            std::fprintf(stderr, "NAV frame %d need_render=%d dirty=%d page=%p done=%d\n",
                trace_frame++, need_render ? 1 : 0, page_dirty ? 1 : 0,
                static_cast<void const*>(cached_page ? &*cached_page : nullptr), done ? 1 : 0);
        if (need_render) {
            window.clear(rendering::Color { 255, 255, 255, 255 });
            if (panel) {
                // A panel is an ordinary document of links, drawn where the page would be, so a click on an entry
                // navigates through the same path a page's own links use (row 4.2).
                auto panel_page = rendering::RenderDocument::create(
                    ui::LibraryPage::build(session, *panel), "", static_cast<float>(kWidth));
                auto panel_surface = panel_page.render_to_surface(kWidth, content_height,
                    rendering::Color { 255, 255, 255, 255 });
                window.blit(panel_surface, 0, kChromeHeight);
            } else {
                render_frame(window);
            }
            // The router only needs to know whether the bar is on screen, so a
            // click at the right edge is routed to the bar instead of the page.
            router.set_scroll_bar(scroll_bar_visible, kWidth, static_cast<int>(rendering::ScrollBar::thickness()));
            // The router hit-tests against the boxes that are about to be painted (row 4.1).
            router.set_chrome_layout(ui::ChromeLayout::compute(session.tab_count(), session.active_index(), kWidth));
            compose_chrome(window);
            host->present(window);
            need_render = false;
            ++presented;
        } else {
            host->present(window);
            ++presented;
        }

        // A frame budget must stop the loop whatever the backend. Without this
        // the SDL host runs until the user closes the window, which hangs a CI
        // runner that has no one to click it.
        // Timers advance by one frame's worth of virtual time. A timer that ran
        // may have mutated the DOM, and only then is the page laid out again: an
        // idle setInterval should not cost a layout and a repaint per frame.
        if (cached_page && page_js.has_timers()) {
            auto const before = rendering::dom_mutation_serial();
            if (page_js.run_timers(16.0)
                && rendering::dom_mutation_serial() != before
                && attribute_mutation_needs_relayout(*cached_page)) {
                cached_page->relayout();
                ++relayouts;
                need_render = true;
            }
        }

        // A keyboard-driven focus, for testing :focus-visible's trigger before Tab
        // exists. Real keyboard focus will set the same field.
        if (!pending_focus_key_done && focus_key_x >= 0 && focus_key_y >= 0 && cached_page) {
            pending_focus_key_done = true;
            auto* target = node_at(*cached_page, page_dom, viewport,
                static_cast<float>(focus_key_x), static_cast<float>(focus_key_y - kChromeHeight));
            visual_state.focus_trigger = rendering::FocusTrigger::Key;
            focused = target;
            if (set_focus(*cached_page, page_js, page_dom, visual_state, target, static_cast<float>(kWidth)))
                ++relayouts;
            need_render = true;
        }

        // A script asked for focus with element.focus() or element.blur(). It is performed here, where
        // the page, the interpreter and the interaction state are all in scope, so it goes through the
        // same path as a click or a Tab key.
        // A navigation requested by script or by a click on a link. The interpreter is deliberately
        // kept: it holds localStorage, and it lives as long as the window. That also means the old
        // page's globals and listeners survive, which is a real limitation, not a design choice.
        if (g_navigation_pending) {
            g_navigation_pending = false;
            std::string const target = g_navigation_request;
            g_navigation_request.clear();
            // A reload replaces the entry it reloads: it is the same document address, and pushing a
            // second copy truncated whatever was ahead of it - so `back()` followed by `forward()`
            // could not move forward, because the reload had just deleted the entry it would move to.
            bool const replace = g_navigation_replace || g_navigation_reload;
            g_navigation_replace = false;
            if (std::getenv("AETHERIS_TRACE_NAV"))
                std::fprintf(stderr, "NAV request=[%s] replace=%d reload=%d\n",
                    target.c_str(), replace ? 1 : 0, g_navigation_reload ? 1 : 0);
            // A fragment change on the same document is a same-document navigation: the URL and the
            // history move, the document is not fetched. Upstream resolves the indicated part and
            // scrolls to it (DOM/Document.cpp, scroll_to_the_fragment); ours re-fetched the page, lost
            // the scroll position and, for an anchor link, landed back at the top.
            auto const without_fragment = [](std::string const& url) {
                auto const hash = url.find('#');
                return hash == std::string::npos ? url : url.substr(0, hash);
            };
            bool const same_document = !replace && cached_page
                && without_fragment(target) == without_fragment(session.current_url());
            if (std::getenv("AETHERIS_TRACE_NAV"))
                std::fprintf(stderr, "NAV target=[%s] current=[%s] same=%d replace=%d page=%d\n",
                    target.c_str(), session.current_url().c_str(), same_document ? 1 : 0,
                    replace ? 1 : 0, cached_page ? 1 : 0);
            if (same_document) {
                auto const hash = target.find('#');
                std::string const fragment = hash == std::string::npos ? std::string() : target.substr(hash + 1);
                if (session.navigate_in_place(target)) {
                    // The script-visible URL moves with the navigation, so `location.hash` reads the new
                    // value on the next statement rather than after a reload that no longer happens.
                    g_current_url = session.current_url();
                    auto const rect = rendering::DocumentInteraction {}.fragment_target(*cached_page, fragment);
                    if (std::getenv("AETHERIS_TRACE_NAV"))
                        std::fprintf(stderr, "NAV fragment=[%s] target=%s y=%.1f scroll_before=%.1f\n",
                            fragment.c_str(), rect ? "found" : "none", rect ? rect->y : -1.0f, viewport.scroll_y());
                    if (rect)
                        viewport.scroll_to(viewport.scroll_x(), rect->y);
                    else
                        viewport.scroll_to(0.0f, 0.0f);
                    if (std::getenv("AETHERIS_TRACE_NAV"))
                        std::fprintf(stderr, "NAV scroll_after=%.1f\n", viewport.scroll_y());
                    need_render = true;
                }
            } else if (session.navigate(target, replace)) {
                page_dirty = true;
                cached_page.reset();
                need_render = true;
            }
        }

        if (!g_focus_requests.empty() && cached_page) {
            // In order: focus then blur leaves nothing focused, and both events fire on the way.
            for (auto* requested : g_focus_requests) {
                if (set_focus(*cached_page, page_js, page_dom, visual_state, requested,
                        static_cast<float>(kWidth)))
                    ++relayouts;
            }
            g_focus_requests.clear();
            need_render = true;
        }

        // A press requested on the command line. It stays pressed on purpose: the
        // frame it produces is the only way to see :active without a display.
        if (!pending_press_done && press_x >= 0 && press_y >= 0 && cached_page) {
            pending_press_done = true;
            auto* node = node_at(*cached_page, page_dom, viewport,
                static_cast<float>(press_x), static_cast<float>(press_y - kChromeHeight));
            if (apply_active_visual_state(*cached_page, visual_state, node, static_cast<float>(kWidth))) {
                ++relayouts;
                need_render = true;
            }
        }

        // A pointer move requested on the command line, once, after the page is up.
        if (!pending_move_done && move_x >= 0 && move_y >= 0 && cached_page) {
            pending_move_done = true;
            auto const before = rendering::dom_mutation_serial();
            bool const element_changed = hover_at(*cached_page, page_js, page_dom, viewport,
                static_cast<float>(move_x), static_cast<float>(move_y - kChromeHeight), hovered);
            bool const restyled = apply_hover_visual_state(*cached_page, visual_state, hovered, static_cast<float>(kWidth));
            if (element_changed && rendering::dom_mutation_serial() != before && attribute_mutation_needs_relayout(*cached_page)) {
                cached_page->relayout();
                ++relayouts;
                need_render = true;
            }
            if (restyled) {
                ++relayouts;
                need_render = true;
            }
        }

        // A click requested on the command line fires once, after the page is up,
        // which is the only way to exercise a handler without a display.
        if (!pending_click_done && click_x >= 0 && click_y >= 0 && cached_page) {
            pending_click_done = true;
            // Focus first: a click focuses what it hit, and :focus must be in the
            // state before the handler runs so the repaint shows it.
            auto* click_target = node_at(*cached_page, page_dom, viewport,
                static_cast<float>(click_x), static_cast<float>(click_y - kChromeHeight));
            // Upstream's focus_candidate_for_position() (Page/EventHandler.cpp:3543): walk up from the
            // hit node to the nearest focusable ancestor, and run the focusing steps with a null
            // fallback - so a click that finds nothing focusable does not move focus at all. A plain
            // div is not a focus target.
            auto* focus_candidate = click_target;
            while (focus_candidate && !is_focusable_node(*focus_candidate))
                focus_candidate = const_cast<rendering::DomNode*>(focus_candidate->parent);
            focused = focus_candidate;
            if (focus_candidate && set_focus(*cached_page, page_js, page_dom, visual_state, focus_candidate,
                    static_cast<float>(kWidth)))
                ++relayouts;
            if (dispatch_click_at(*cached_page, page_js, page_dom, viewport,
                    static_cast<float>(click_x), static_cast<float>(click_y - kChromeHeight)))
                need_render = true;

            // A click on a link navigates, unless a handler called preventDefault(). The anchor may be
            // above the node that was hit, which is why this walks up.
            auto* link = click_target;
            while (link && !(link->name == "a" && link->attribute("href")))
                link = const_cast<rendering::DomNode*>(link->parent);
            if (link && !page_js.last_event_prevented()) {
                g_navigation_request = resolve_url(g_current_url, *link->attribute("href"));
                g_navigation_pending = true;
            }
        }

        // A key requested on the command line fires once, after the page is up.
        if (!pending_key_done && !key_to_press.empty() && cached_page) {
            pending_key_done = true;
            auto const before = rendering::dom_mutation_serial();
            if (key_to_press == "tab") {
                auto* next = next_focusable(*cached_page, focused);
                visual_state.focus_trigger = rendering::FocusTrigger::Key;
                focused = next;
                if (set_focus(*cached_page, page_js, page_dom, visual_state, next, static_cast<float>(kWidth)))
                    ++relayouts;
                // Focus changes the cascade, not the DOM, so the fingerprint below
                // stays equal: without this the frame is never redrawn and the focus
                // ring never appears. Two sessions lost to this line.
                need_render = true;
            } else {
                dispatch_key_to_focus(page_js, page_dom, focused, key_to_press);
            }
            if (rendering::dom_mutation_serial() != before && attribute_mutation_needs_relayout(*cached_page)) {
                cached_page->relayout();
                ++relayouts;
                need_render = true;
            }
        }

        if (frames > 0 && presented >= frames)
            done = true;

        while (host->poll(event)) {
            auto action = router.handle(event);
            if (action.kind == ui::InputAction::Kind::None)
                continue;
            ++handled;
            switch (action.kind) {
            case ui::InputAction::Kind::Quit:
                done = true;
                break;
            case ui::InputAction::Kind::Navigate:
                if (session.navigate(action.text)) {
                    router.set_address_text(session.current_url());
                    page_dirty = true;
                    need_render = true;
                }
                break;
            case ui::InputAction::Kind::Reload:
                page_dirty = true;
                need_render = true;
                break;
            case ui::InputAction::Kind::Scroll:
                // SDL reports a positive wheel delta for "away from the user",
                // which scrolls up. The sign is inverted exactly once, here.
                viewport.scroll_by(0.0f, -static_cast<float>(action.delta) * static_cast<float>(ui::InputRouter::scroll_pixels_per_notch()));
                need_render = true;
                break;
            case ui::InputAction::Kind::ScrollPage:
                viewport.scroll_by(0.0f, -static_cast<float>(action.delta) * static_cast<float>(content_height) * 0.9f);
                need_render = true;
                break;
            case ui::InputAction::Kind::ScrollEdge:
                viewport.scroll_to(0.0f, action.delta < 0 ? 0.0f : document_height);
                need_render = true;
                break;
            case ui::InputAction::Kind::PageHover:
                if (cached_page) {
                    auto const before = rendering::dom_mutation_serial();
                    bool const element_changed = hover_at(*cached_page, page_js, page_dom, viewport,
                        static_cast<float>(action.x), static_cast<float>(action.y - kChromeHeight), hovered);
                    bool const restyled = apply_hover_visual_state(*cached_page, visual_state, hovered, static_cast<float>(kWidth));
                    if (element_changed && rendering::dom_mutation_serial() != before && attribute_mutation_needs_relayout(*cached_page)) {
                        cached_page->relayout();
                        ++relayouts;
                        need_render = true;
                    }
                    if (restyled) {
                        ++relayouts;
                        need_render = true;
                    }
                }
                break;
            case ui::InputAction::Kind::FocusNext:
                if (cached_page) {
                    auto* next = next_focusable(*cached_page, focused);
                    visual_state.focus_trigger = rendering::FocusTrigger::Key;
                    focused = next;
                    if (set_focus(*cached_page, page_js, page_dom, visual_state, next, static_cast<float>(kWidth)))
                        ++relayouts;
                    need_render = true;
                }
                break;
            case ui::InputAction::Kind::Key:
                if (cached_page) {
                    auto const before = rendering::dom_mutation_serial();
                    dispatch_key_to_focus(page_js, page_dom, focused, action.text);
                    if (rendering::dom_mutation_serial() != before && attribute_mutation_needs_relayout(*cached_page)) {
                        cached_page->relayout();
                        ++relayouts;
                        need_render = true;
                    }
                }
                break;
            case ui::InputAction::Kind::Release:
                if (cached_page
                    && apply_active_visual_state(*cached_page, visual_state, nullptr, static_cast<float>(kWidth))) {
                    ++relayouts;
                    need_render = true;
                }
                break;
            case ui::InputAction::Kind::PageClick:
                if (cached_page) {
                    auto* pressed = node_at(*cached_page, page_dom, viewport,
                        static_cast<float>(action.x), static_cast<float>(action.y - kChromeHeight));
                    if (apply_active_visual_state(*cached_page, visual_state, pressed, static_cast<float>(kWidth))) {
                        ++relayouts;
                        need_render = true;
                    }
                }
                if (cached_page && dispatch_click_at(*cached_page, page_js, page_dom, viewport,
                        static_cast<float>(action.x), static_cast<float>(action.y - kChromeHeight)))
                    need_render = true;
                break;
            case ui::InputAction::Kind::ScrollTrack: {
                // Click on the bar: the geometry is recomputed from the current
                // scroll position, so the thumb lands under the pointer.
                auto bar = rendering::ScrollBar::geometry(static_cast<float>(kWidth), static_cast<float>(content_height), document_height, viewport.scroll_y());
                viewport.scroll_to(0.0f, rendering::ScrollBar::scroll_for_pointer(bar, static_cast<float>(action.y - kChromeHeight)));
                need_render = true;
                break;
            }
            case ui::InputAction::Kind::NewTab:
                session.new_tab();
                need_render = true;
                break;
            case ui::InputAction::Kind::ActivateTab:
                if (session.activate_tab(action.index)) {
                    // Switching tabs loads the tab's URL, the same request a history move makes.
                    g_navigation_request = session.current_url();
                    g_navigation_pending = true;
                    need_render = true;
                }
                break;
            case ui::InputAction::Kind::CloseTab:
                // A click on a tab's close button closes that tab; the keyboard shortcut closes the active one.
                if (action.index < session.tab_count() && action.index != session.active_index())
                    session.activate_tab(action.index);
                session.close_active_tab();
                need_render = true;
                break;
            case ui::InputAction::Kind::ShowHistory:
                panel = panel == ui::LibraryPage::Mode::History ? std::nullopt
                                                                : std::optional<ui::LibraryPage::Mode> { ui::LibraryPage::Mode::History };
                need_render = true;
                break;
            case ui::InputAction::Kind::ShowBookmarks:
                panel = panel == ui::LibraryPage::Mode::Bookmarks ? std::nullopt
                                                                  : std::optional<ui::LibraryPage::Mode> { ui::LibraryPage::Mode::Bookmarks };
                need_render = true;
                break;
            case ui::InputAction::Kind::ClosePanel:
                if (panel) {
                    panel.reset();
                    need_render = true;
                }
                break;
            case ui::InputAction::Kind::ToggleBookmark:
                session.toggle_bookmark();
                need_render = true;
                break;
            case ui::InputAction::Kind::ToggleZen:
                std::cout << "  zen mode: " << (router.zen_mode() ? "on" : "off") << std::endl;
                break;
            default:
                break;
            }
            if (done)
                break;
        }
    }
    host->close();

    // Free the JS handles the host owns before the interpreter goes: a runtime
    // freed with live objects trips an assertion in QuickJS, and this is what
    // --render does at the end of its own path.
    page_dom.release();

    std::cout << "Window " << kWidth << "x" << kHeight << " (chrome " << kChromeHeight
              << "px, content " << content_height << "px, document "
              << static_cast<int>(document_height) << "px), backend: "
              << (ui::has_display_backend() ? "display" : "headless")
              << ", tabs: " << session.tab_count() << ", events handled: " << handled
              << ", relayouts: " << relayouts
              << ", layouts: " << (cached_page ? cached_page->layout_passes() : 0)
              << ", key checks: " << (cached_page ? cached_page->key_checks() : 0)
              << ", layout nodes: " << (cached_page ? cached_page->laid_out_nodes() : 0)
              << ", resolves: " << (cached_page ? cached_page->last_style_stats().resolves : 0)
              << ", style hits: " << (cached_page ? cached_page->last_style_stats().hits : 0)
              << std::endl;
    return 0;
}


static int run_render_window(std::string const& url, std::string const& output_path, float scroll_y = 0.0f)
{
    if (network::Socket::initialize() != network::SocketError::None) {
        std::cerr << "Failed to initialize network subsystem" << std::endl;
        return 1;
    }
    rendering::Url document_url = rendering::Url::parse(url);
    SecureFetcher fetcher(document_url);
    auto loaded = fetcher.fetch(rendering::ResourceRequestContext::Document, document_url);
    network::Socket::cleanup();
    if (!loaded.succeeded()) {
        std::cerr << "Fetch failed: " << loaded.message << std::endl;
        return 1;
    }
    std::string body(loaded.resource->bytes.begin(), loaded.resource->bytes.end());

    constexpr int kWidth = 1024;
    constexpr int kHeight = 768;
    int content_height = kHeight - kChromeHeight;

    auto style = collect_page_style(body, url, fetcher);
    auto page = rendering::RenderDocument::create(body, style.css, static_cast<float>(kWidth), nullptr, url);

    // `--scroll-y` renders the page as it looks scrolled, which is the only way
    // to look at a long page from a headless machine.
    float document_height = std::max(page.document_rect().height, static_cast<float>(content_height));
    float clamped_scroll = std::clamp(scroll_y, 0.0f, std::max(0.0f, document_height - static_cast<float>(content_height)));
    auto page_surface = page.render_to_surface(kWidth, content_height, rendering::Color { 255, 255, 255, 255 },
        rendering::PaintOffset { 0.0f, clamped_scroll });
    rendering::ScrollBar::paint(page_surface,
        rendering::ScrollBar::geometry(static_cast<float>(kWidth), static_cast<float>(content_height), document_height, clamped_scroll),
        rendering::Color { 0, 0, 0, 24 },
        rendering::Color { 80, 80, 88, 200 });

    // The title comes from the page so the tab reads like a real browser.
    std::string title = document_title(page.document().root());

    // This path renders one page with no session, so it shows a one-tab strip. Same layout code, so the tab is where
    // a click would be in the window.
    ui::BrowserSession single;
    single.open(url);
    single.set_page_title(title);
    auto chrome = rendering::RenderDocument::create(chrome_document(single, url), "", static_cast<float>(kWidth));
    auto chrome_surface = chrome.render_to_surface(kWidth, kChromeHeight, rendering::Color { 32, 33, 36, 255 });

    auto window = rendering::SoftwareSurface(kWidth, kHeight, rendering::Color { 255, 255, 255, 255 });
    window.blit(chrome_surface, 0, 0);
    window.blit(page_surface, 0, kChromeHeight);

    if (!rendering::ImageOutput::write_ppm(window, output_path)) {
        std::cerr << "Failed to write " << output_path << std::endl;
        return 1;
    }
    std::cout << "Rendered window " << kWidth << "x" << kHeight << " (chrome " << kChromeHeight
              << "px, content " << content_height << "px, document " << static_cast<int>(document_height)
              << "px, scroll " << static_cast<int>(clamped_scroll) << "px) to " << output_path << std::endl;
    return 0;
}


// Loads a page the way a browser does: parse, run the inline scripts against
// the DOM, then lay the result out. The window loop used to call
// RenderDocument::create directly, so no script ever ran in a real window -
// pages that build their content in JavaScript rendered blank.
//
// This is the script path from run_render, kept as one function so the window
// gets identical behaviour. run_render still has its own copy because --dispatch
// and --click need the live host and interpreter after layout; folding the two
// together is the next refactor, and it is recorded in HANDOVER.md.
static rendering::RenderDocument create_scripted_document(
    std::string const& html, std::string const& css, std::string const& url,
    rendering::ResourceCache* resources,
    aetheris::js::Interpreter& js, DomHost& dom,
    std::shared_ptr<std::array<double, 4>> viewport_state, double timer_budget_ms,
    SecureFetcher* fetcher)
{
    auto trace_phase = [](char const* what) {
        if (std::getenv("AETHERIS_TRACE_DOC"))
            std::fprintf(stderr, "DOC %s\n", what);
    };
    trace_phase("parse begin");
    // Parse the document so inline scripts can read and mutate it.
    rendering::Document document = rendering::Document::parse_html(html);
    trace_phase("parse done");

    rendering::CssParser computed_parser;
    rendering::CssStyleSheet computed_sheet = computed_parser.parse(css);
    g_stylesheet = &computed_sheet;

    std::string script_output;

    // Scroll state for scripts. A static render has no window, so it lives here:
    // scroll_x, scroll_y, document height (filled in once layout exists) and the
    // client height. A scroll request is recorded rather than performed, because
    // --render produces one image, and that is enough for a page's scroll handler
    // to run and read back what it set.
    js.print_handler = [](std::string const& line) { std::cout << "  [js] " << line << std::endl; };
    js.write_handler = [&script_output](std::string const& text) { script_output += text; };

    g_js = &js;

    dom.initialize(js.context());
    dom.collect(document.root());
    build_handles(document.root(), dom);

    // document / window surface
    {
        g_document_root = &document.root();
        // `html_element`, not `html`: an outer std::string html is in scope here
        // and C4456 (declaration hides previous local) is fatal under /WX.
        auto* html_element = find_element(document.root(), "html");
        auto* body = find_element(document.root(), "body");
        auto* head = find_element(document.root(), "head");
        JSValue document_object = js.document_object();
        if (body)
            JS_SetPropertyStr(js.context(), document_object, "body", dom.handle_for(body));
        if (head)
            JS_SetPropertyStr(js.context(), document_object, "head", dom.handle_for(head));
        if (html_element)
            JS_SetPropertyStr(js.context(), document_object, "documentElement", dom.handle_for(html_element));

        JS_SetPropertyStr(js.context(), document_object, "getComputedStyle",
            JS_NewCFunction(js.context(), js_get_computed_style, "getComputedStyle", 1));

        JSAtom title_atom = JS_NewAtom(js.context(), "title");
        JS_DefinePropertyGetSet(js.context(), document_object, title_atom,
            JS_NewCFunction(js.context(), js_document_title_get, "get title", 0),
            JS_NewCFunction(js.context(), js_document_title_set, "set title", 1),
            JS_PROP_CONFIGURABLE);
        JS_FreeAtom(js.context(), title_atom);

        JSValue window_object = js.window_object();
        JS_SetPropertyStr(js.context(), window_object, "innerWidth", JS_NewInt32(js.context(), 1024));
        JS_SetPropertyStr(js.context(), window_object, "innerHeight", JS_NewInt32(js.context(), 768));

        js.viewport_metrics = [viewport_state]() {
            aetheris::js::ViewportMetrics metrics;
            metrics.scroll_x = (*viewport_state)[0];
            metrics.scroll_y = (*viewport_state)[1];
            metrics.scroll_width = 1024;
            metrics.scroll_height = (*viewport_state)[2];
            metrics.client_width = 1024;
            metrics.client_height = (*viewport_state)[3];
            return metrics;
        };
        js.scroll_target = [viewport_state](double x, double y) {
            (*viewport_state)[0] = x;
            (*viewport_state)[1] = y;
        };

        // Pages reach for documentElement and body as often as for
        // document.scrollingElement, so all three carry the scroll properties.
        for (auto const* name : { "body", "documentElement" }) {
            JSValue element = JS_GetPropertyStr(js.context(), document_object, name);
            if (!JS_IsUndefined(element) && !JS_IsNull(element))
                aetheris::js::install_viewport_properties(js.context(), element);
            JS_FreeValue(js.context(), element);
        }
    }

    js.element_lookup = [&dom](std::string const& id) -> JSValue {
        auto it = dom.handle_by_id.find(id);
        return it == dom.handle_by_id.end() ? JS_NULL : JS_DupValue(dom.context, it->second);
    };
    js.create_element = [&dom](std::string const& tag) -> JSValue {
        auto node = std::make_unique<rendering::DomNode>(rendering::DomNodeType::Element, tag, "");
        auto* raw = node.get();
        dom.pending[raw] = std::move(node);
        return dom.handle_for(raw);
    };
    js.query_selector = [&dom, &document](std::string const& selector) -> JSValue {
        std::vector<std::vector<SimpleSelector>> chains;
        if (!parse_selector(selector, chains))
            return JS_NULL;
        std::vector<rendering::DomNode*> matches;
        collect_selector_matches(document.root(), dom, chains, matches);
        if (matches.empty())
            return JS_NULL;
        auto it = dom.handle_by_node.find(matches.front());
        return it != dom.handle_by_node.end() ? JS_DupValue(dom.context, it->second) : JS_NULL;
    };
    js.query_selector_all = [&dom, &document](std::string const& selector) -> JSValue {
        JSValue array = JS_NewArray(dom.context);
        std::vector<std::vector<SimpleSelector>> chains;
        if (!parse_selector(selector, chains))
            return array;
        std::vector<rendering::DomNode*> matches;
        collect_selector_matches(document.root(), dom, chains, matches);
        uint32_t index = 0;
        for (auto* node : matches) {
            auto it = dom.handle_by_node.find(node);
            if (it != dom.handle_by_node.end())
                JS_SetPropertyUint32(dom.context, array, index++, JS_DupValue(dom.context, it->second));
        }
        return array;
    };

    // Content-Security-Policy: inline scripts only run when the policy allows, and every fetch from here on is
    // checked against the same policy's fetch directives (row 5a).
    auto policy = content_security_policy(document.root());
    if (!policy.empty() && fetcher)
        fetcher->secure->set_content_security_policy(rendering::ContentSecurityPolicy::parse(policy));
    if (!rendering::ContentSecurityPolicy::parse(policy).allows_inline_script()) {
        std::cerr << "  [js] inline scripts blocked by Content-Security-Policy" << std::endl;
    } else for (auto const& script : extract_scripts(html)) {
        trace_phase("script begin");
        js.run(script);
        trace_phase("script done");
        if (js.has_error()) {
            std::cerr << "  [js] " << js.error() << std::endl;
            break;
        }
    }
    if (!js.has_error()) {
        trace_phase("events begin");
        js.dispatch_event("DOMContentLoaded");
        js.dispatch_event("load");
        trace_phase("events done");
        // A one-shot render drains the timer queue (negative budget); a window
        // advances one frame at a time, because draining there would fire a
        // repeating timer a thousand times before the first frame.
        trace_phase("timers begin");
        if (timer_budget_ms < 0)
            js.run_event_loop();
        else
            js.run_timers(timer_budget_ms);
        trace_phase("timers done");
    }

    // textContent writes go straight to the DOM through its accessor, so no
    // post-script reconciliation pass is needed (an earlier pass compared a
    // stale snapshot and could free nodes whose handles were still live).

    // document.write output is appended to the body.
    if (!script_output.empty()) {
        auto* body = find_element(document.root(), "body");
        auto text_node = std::make_unique<rendering::DomNode>(rendering::DomNodeType::Text, "", script_output);
        (body ? *body : document.root()).append_child(std::move(text_node));
    }

    trace_phase("layout begin");
    auto built = rendering::RenderDocument::create_from_document(std::move(document), css, 1024.0f, resources, url);
    trace_phase("layout done");
    return built;
}

static int run_render(std::string const& url, std::string const& output_path, std::string const& dispatch_id = {}, std::string const& dispatch_type = {}, float click_x = -1, float click_y = -1) {
    auto const trace_begin = std::chrono::steady_clock::now();
    auto const trace_at = [&](char const* what) {
        if (std::getenv("AETHERIS_TRACE_RENDER"))
            std::fprintf(stderr, "TRACE %-28s %.1f ms\n", what,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - trace_begin).count());
    };
    if (network::Socket::initialize() != network::SocketError::None) {
        std::cerr << "Failed to initialize network subsystem" << std::endl;
        return 1;
    }

    rendering::Url document_url = rendering::Url::parse(url);
    SecureFetcher fetcher(document_url);
    auto document_result = fetcher.fetch(rendering::ResourceRequestContext::Document, document_url);
    if (!document_result.succeeded()) {
        network::Socket::cleanup();
        std::cerr << "Fetch failed: " << document_result.message << std::endl;
        return 1;
    }
    std::string document_body(document_result.resource->bytes.begin(), document_result.resource->bytes.end());

    // Discover and fetch the document's linked resources: stylesheets are
    // concatenated into the cascade, images are cached for the engine to resolve.
    rendering::ResourceCache resources;
    rendering::ResourceLoader loader(resources);
    rendering::Url base = rendering::Url::parse(url);
    rendering::ResourceData document_resource {
        rendering::ResourceType::Document,
        url,
        std::vector<unsigned char>(document_body.begin(), document_body.end()),
    };

    std::string css;
    std::size_t fetched_count = 0;
    auto const dependencies = loader.discover_dependencies_detailed(document_resource, base);

    // Fetch them concurrently (row 0.4). A real page carries a hundred or more of these and each one
    // costs a round trip: the wikipedia page has 188 and spent 57 s of its 58 s waiting for them one
    // at a time. Eight workers measured 7.6x against a loopback server (tools/fetch-cost.cpp).
    //
    // Each worker gets its **own** `SecureFetcher`, and that is not an optimisation but the only
    // correct shape: a fetch writes into the resource cache, the backend list and the security
    // policy's decision, so sharing one across threads would be a data race rather than a saving.
    // Only the finished results cross back, and they are applied here on one thread, in discovery
    // order, so the cascade is built in the order the document declared.
    // The kind comes from where the URL appeared, not from its suffix: a stylesheet served by a script
    // endpoint has no `.css` to look for, and testing for one meant a real page's stylesheets were cached
    // as images and never entered the cascade (row 0.7).
    auto const context_for = [](rendering::ResourceDependency const& dependency) {
        return dependency.is_stylesheet ? rendering::ResourceRequestContext::Style
                                        : rendering::ResourceRequestContext::Image;
    };
    std::vector<std::optional<rendering::ResourceLoadResult>> fetched_results(dependencies.size());

    // One worker per sixteen resources, capped at eight: below that the threads cost more than the
    // round trips they overlap, and this box has a hard thread limit.
    std::size_t const worker_count = std::min<std::size_t>(8, dependencies.size() / 16);
    if (std::getenv("AETHERIS_TRACE_RENDER"))
        std::fprintf(stderr, "TRACE dependencies=%zu workers=%zu\n", dependencies.size(), worker_count);
    if (worker_count < 2) {
        for (std::size_t i = 0; i < dependencies.size(); ++i)
            fetched_results[i] = fetcher.fetch(context_for(dependencies[i]), dependencies[i].url);
    } else {
        std::atomic<std::size_t> next { 0 };
        std::vector<std::thread> workers;
        workers.reserve(worker_count);
        for (std::size_t w = 0; w < worker_count; ++w) {
            workers.emplace_back([&]() {
                SecureFetcher worker_fetcher(base);
                worker_fetcher.secure->set_content_security_policy(fetcher.secure->content_security_policy());
                for (;;) {
                    std::size_t const i = next.fetch_add(1);
                    if (i >= dependencies.size())
                        break;
                    fetched_results[i] = worker_fetcher.fetch(context_for(dependencies[i]), dependencies[i].url);
                }
            });
        }
        for (auto& worker : workers)
            worker.join();
    }

    for (std::size_t i = 0; i < dependencies.size(); ++i) {
        if (!fetched_results[i] || !fetched_results[i]->succeeded())
            continue;
        auto const& dependency = dependencies[i];
        bool const is_css = dependency.is_stylesheet;
        auto fetched = std::move(*fetched_results[i]);
        ++fetched_count;
        std::string fetched_text(fetched.resource->bytes.begin(), fetched.resource->bytes.end());
        std::vector<unsigned char> bytes(fetched.resource->bytes.begin(), fetched.resource->bytes.end());
        if (is_css) {
            if (!css.empty())
                css.push_back('\n');
            css += fetched_text;
            resources.put({ rendering::ResourceType::Stylesheet, dependency.url.serialized(), std::move(bytes) });
        } else {
            resources.put({ rendering::ResourceType::Image, dependency.url.serialized(), std::move(bytes) });
        }
    }
    // Inline <style> blocks join the cascade after the linked stylesheets.
    for (auto const& block : extract_styles(document_body)) {
        if (!css.empty())
            css.push_back('\n');
        css += block;
    }

    // `@font-face` names its file inside the stylesheet, so it is not in the dependency list. Fetch the
    // remote ones into the cache here, where `RenderDocument` looks them up: a page whose fonts arrive over
    // the network used to render every glyph in the engine's own face (row 3d1's tail).
    {
        std::regex const face_source(R"(@font-face\s*\{[^}]*?src\s*:\s*url\(\s*[\"']?([^\"')]+)[\"']?\s*\))",
            std::regex::icase);
        std::vector<std::string> seen;
        for (std::sregex_iterator it(css.begin(), css.end(), face_source), end; it != end; ++it) {
            std::string const source = (*it)[1].str();
            if (source.rfind("http://", 0) != 0 && source.rfind("https://", 0) != 0)
                continue; // data: and file: are read by the engine itself
            if (std::find(seen.begin(), seen.end(), source) != seen.end())
                continue;
            seen.push_back(source);
            auto fetched = fetcher.fetch(rendering::ResourceRequestContext::Font, rendering::Url::parse(source));
            if (!fetched.succeeded())
                continue;
            resources.put({ rendering::ResourceType::Font, source,
                std::vector<unsigned char>(fetched.resource->bytes.begin(), fetched.resource->bytes.end()) });
        }
    }

    trace_at("resources fetched");
    network::Socket::cleanup();

    // Scripts run through the same path the window uses, so --render and --window
    // cannot drift. A negative budget drains the timer queue, which is what a
    // one-shot render wants.
    std::string html = document_body;
    aetheris::js::Interpreter js;
    DomHost dom;
    auto viewport_state = std::make_shared<std::array<double, 4>>(std::array<double, 4> { 0.0, 0.0, 0.0, 768.0 });
    auto const trace_start = std::chrono::steady_clock::now();
    auto rendered = create_scripted_document(html, css, url, &resources, js, dom, viewport_state, -1.0, &fetcher);
    if (std::getenv("AETHERIS_TRACE_RENDER"))
        std::fprintf(stderr, "TRACE create_scripted_document %.1f ms\n",
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - trace_start).count());

    // --dispatch fires an element event after load, then re-lays out so the
    // handler's changes are in the image. It lives here rather than in the shared
    // loader because a window has no use for it.
    if (!js.has_error() && !dispatch_id.empty() && !dispatch_type.empty()) {
        auto it = dom.handle_by_id.find(dispatch_id);
        auto* node = it != dom.handle_by_id.end()
            ? static_cast<rendering::DomNode*>(JS_GetOpaque(it->second, g_dom_node_class))
            : nullptr;
        if (node) {
            dispatch_bubbling(js, dom, node, dispatch_type);
            rendered.relayout();
        } else {
            std::cerr << "  [js] no element with id '" << dispatch_id << "'" << std::endl;
        }
    }

    // Layout exists now, so scripts can be told how tall the page really is.
    (*viewport_state)[2] = static_cast<double>(rendered.layout_root().rect.height);

    // Geometry-based dispatch, through the same function the window uses, so a
    // click behaves identically in a render and in a window.
    if (click_x >= 0 && click_y >= 0) {
        rendering::Viewport click_viewport(1024.0f, 768.0f);
        click_viewport.set_document_size(1024.0f, 768.0f);
        dispatch_click_at(rendered, js, dom, click_viewport, click_x, click_y);
    }
    // A PPM has no alpha channel, so render onto an opaque white background to
    // keep the output viewable.
    auto const paint_start = std::chrono::steady_clock::now();
    auto surface = rendered.render_to_surface(1024, 768, rendering::Color { 255, 255, 255, 255 });
    if (std::getenv("AETHERIS_TRACE_RENDER"))
        std::fprintf(stderr, "TRACE render_to_surface %.1f ms\n",
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - paint_start).count());
    if (!rendering::ImageOutput::write_ppm(surface, output_path)) {
        std::cerr << "Failed to write " << output_path << std::endl;
        return 1;
    }

    std::cout << "Rendered " << document_body.size() << " bytes of HTML and "
              << css.size() << " bytes of CSS (" << fetched_count << " linked resources) to "
              << output_path << std::endl;

    // Drop the host's references before the JS runtime goes away.
    dom.release();
    return 0;
}

int main(int argc, char* argv[]) {
    using namespace std::chrono;

    // Optional element event to fire after a --render load.
    std::string dispatch_id;
    std::string dispatch_type;
    float click_x = -1;
    float click_y = -1;
    for (int i = 1; i < argc; ++i) {
        std::string option(argv[i]);
        if (option == "--dispatch" && i + 2 < argc) {
            dispatch_id = argv[i + 1];
            dispatch_type = argv[i + 2];
        } else if (option == "--click" && i + 1 < argc) {
            std::string value(argv[i + 1]);
            auto comma = value.find(',');
            if (comma != std::string::npos) {
                click_x = std::stof(value.substr(0, comma));
                click_y = std::stof(value.substr(comma + 1));
            }
        }
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: aetheris [options]\n"
                      << "  --smoke-test   Initialize all subsystems, run a few headless\n"
                      << "                 frames, print stats and exit 0 (used by CI).\n"
                      << "  --fetch <url>  Fetch a URL over HTTP and print the status,\n"
                      << "                 content type and body size.\n"
                      << "  --render <url> [out.ppm]\n"
                      << "  --render-window <url> [out.ppm]   page composited under the browser chrome\n"
                      << "                 Add `--scroll-y <px>` after the path to render a scrolled view.\n"
                      << "  --window <url> [frames] [dir]     run the window loop (headless without SDL2)\n"
                      << "                 Fetch a URL, render it and write a PPM image.\n"
                      << "  --eval <script>  Run a JavaScript snippet and print its output.\n"
                      << "  --dispatch <id> <type>\n"
                      << "                 With --render, fire an event on the element with this id.\n"
                      << "  --click <x>,<y>\n"
                      << "                 With --render, hit-test the point and fire a click there.\n"
                      << "  --dump-dom <url>  Fetch a URL and print the parsed DOM.\n"
                      << "  --version      Print version information.\n"
                      << "  -h, --help     Print this help.\n";
            return 0;
        }
        if (arg == "--version" || arg == "-v") {
            std::cout << "Aetheris Browser v0.6.2" << std::endl;
            return 0;
        }
        if (arg == "--smoke-test") {
            return run_smoke_test();
        }
        if (arg == "--fetch" && i + 1 < argc) {
            return run_fetch(argv[++i]);
        }
        if (arg == "--eval" && i + 1 < argc) {
            return run_eval(argv[++i]);
        }
        if (arg == "--dump-dom" && i + 1 < argc) {
            return run_dump_dom(argv[++i]);
        }
        if (arg == "--render" && i + 1 < argc) {
            std::string url = argv[++i];
            std::string output_path = "aetheris-render.ppm";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                output_path = argv[++i];
            }
            return run_render(url, output_path, dispatch_id, dispatch_type, click_x, click_y);
        }
        if (arg == "--window" && i + 1 < argc) {
            std::string url = argv[++i];
            int frames = 1;
            std::string frame_directory;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                frames = std::atoi(argv[++i]);
            if (i + 1 < argc && argv[i + 1][0] != '-')
                frame_directory = argv[++i];
            int click_x = -1;
            int click_y = -1;
            if (i + 1 < argc && std::string(argv[i + 1]) == "--click") {
                // Both spellings: `--click 200,50` matches --render, and
                // `--click 200 50` reads better with a window's other options.
                std::string value(argv[i + 2 < argc ? i + 2 : i + 1]);
                auto comma = value.find(',');
                if (comma != std::string::npos) {
                    click_x = std::atoi(value.substr(0, comma).c_str());
                    click_y = std::atoi(value.substr(comma + 1).c_str());
                    i += 2;
                } else if (i + 3 < argc) {
                    click_x = std::atoi(argv[i + 2]);
                    click_y = std::atoi(argv[i + 3]);
                    i += 3;
                }
            }
            std::string key_to_press;
            if (i + 1 < argc && std::string(argv[i + 1]) == "--key" && i + 2 < argc) {
                key_to_press = argv[i + 2];
                i += 2;
            }
            int move_x = -1;
            int move_y = -1;
            if (i + 1 < argc && std::string(argv[i + 1]) == "--move" && i + 3 < argc) {
                move_x = std::atoi(argv[i + 2]);
                move_y = std::atoi(argv[i + 3]);
                i += 3;
            }
            int press_x = -1;
            int press_y = -1;
            if (i + 1 < argc && std::string(argv[i + 1]) == "--press" && i + 3 < argc) {
                press_x = std::atoi(argv[i + 2]);
                press_y = std::atoi(argv[i + 3]);
                i += 3;
            }
            int focus_key_x = -1;
            int focus_key_y = -1;
            if (i + 1 < argc && std::string(argv[i + 1]) == "--focus-key" && i + 3 < argc) {
                focus_key_x = std::atoi(argv[i + 2]);
                focus_key_y = std::atoi(argv[i + 3]);
                i += 3;
            }
            return run_window(url, frames, frame_directory, click_x, click_y, key_to_press,
                move_x, move_y, press_x, press_y, focus_key_x, focus_key_y);
        }
        // Same page, but composited under the compact browser chrome.
        if (arg == "--render-window" && i + 1 < argc) {
            std::string url = argv[++i];
            std::string output_path = "aetheris-window.ppm";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                output_path = argv[++i];
            }
            float scroll_y = 0.0f;
            if (i + 1 < argc && std::string(argv[i + 1]) == "--scroll-y" && i + 2 < argc) {
                scroll_y = std::stof(argv[i + 2]);
                i += 2;
            }
            return run_render_window(url, output_path, scroll_y);
        }
    }

    std::cout << "=== AETHERIS BROWSER v0.6.2 ===" << std::endl;
    std::cout << "Hyper-optimized, zero-bloat cross-platform browser" << std::endl;
    std::cout << std::endl;
    
    // Initialize network subsystem
    auto net_error = network::Socket::initialize();
    if (net_error != network::SocketError::None) {
        std::cerr << "Failed to initialize network subsystem" << std::endl;
        return 1;
    }
    
    // Create window manager
    auto window = std::make_unique<ui::WindowManager>();
    ui::WindowConfig config;
    config.width = 1280;
    config.height = 720;
    config.title = "Aetheris Browser";
    
    if (!window->create(config)) {
        std::cerr << "Failed to create window" << std::endl;
        network::Socket::cleanup();
        return 1;
    }
    
    // Create ghost cache
    auto ghost_cache = std::make_unique<cache::GhostCache>();
    
    // Setup debug overlay
    DebugOverlay debug_overlay;
    
    // Event handler
    bool should_close = false;
    window->set_event_callback([&](ui::EventType type, const ui::KeyEvent& key, const ui::MouseEvent& mouse) {
        (void)mouse;
        switch (type) {
            case ui::EventType::Close:
                should_close = true;
                break;
                
            case ui::EventType::KeyPress:
                if (key.is_pressed && key.keycode == 27) {  // ESC key
                    should_close = true;
                } else if (key.is_pressed && key.keycode == 122) {  // F11
                    window->toggle_fullscreen();
                }
                break;
                
            default:
                break;
        }
    });
    
    window->show();
    
    // Main loop timing
    [[maybe_unused]] auto last_time = steady_clock::now();
    double fps = 0.0;
    int frame_count = 0;
    auto fps_start = steady_clock::now();
    
    std::cout << "Window created successfully!" << std::endl;
    std::cout << "Starting main event loop..." << std::endl;
    std::cout << std::endl;
    
    // Main event loop
    while (!should_close) {
        // Poll events
        if (!window->poll_events()) {
            break;
        }

        // Small idle sleep so the loop never busy-spins when there are no
        // events pending (keeps CPU usage flat in headless/X11 mode).
        std::this_thread::sleep_for(8ms);
        
        // Clear and render
        window->clear();
        
        // Calculate FPS
        auto current_time = steady_clock::now();
        ++frame_count;
        
        auto elapsed = duration_cast<milliseconds>(current_time - fps_start).count();
        if (elapsed >= 1000) {
            fps = static_cast<double>(frame_count) * 1000.0 / static_cast<double>(elapsed);
            frame_count = 0;
            fps_start = current_time;
            
            // Update debug overlay stats
            debug_overlay.update_stats(
                fps,
                ghost_cache->total_compressed_bytes(),
                ghost_cache->active_entries(),
                ghost_cache->compression_ratio()
            );
        }
        
        // Draw debug overlay
        window->draw_debug_overlay(debug_overlay.get_text());
        
        window->swap_buffers();
        
        // Small delay to prevent CPU spinning (remove in production with proper vsync)
        std::this_thread::yield();
    }
    
    std::cout << std::endl;
    std::cout << "Shutting down..." << std::endl;
    std::cout << "Final cache statistics:" << std::endl;
    std::cout << "  Active entries: " << ghost_cache->active_entries() << std::endl;
    std::cout << "  Compressed bytes: " << ghost_cache->total_compressed_bytes() << std::endl;
    std::cout << "  Uncompressed bytes: " << ghost_cache->total_uncompressed_bytes() << std::endl;
    std::cout << "  Compression ratio: " << ghost_cache->compression_ratio() << "x" << std::endl;
    
    // Cleanup
    ghost_cache->clear_all();
    window->destroy();
    network::Socket::cleanup();
    
    std::cout << "Aetheris closed successfully." << std::endl;
    
    return 0;
}

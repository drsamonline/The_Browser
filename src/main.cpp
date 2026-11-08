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
#include "style.hpp"
#include "image_output.hpp"
#include "http_resource_backend.hpp"
#include "resource_backend.hpp"
#include "resource_loader.hpp"
#include "secure_resource_loader.hpp"
#include "ui/window_host.hpp"
#include "resource.hpp"
#include "url.hpp"

#include "js.hpp"
#include "document_interaction.hpp"
#include "viewport.hpp"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <map>
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

    if (!window->create(config)) {
        std::cerr << "Failed to create window" << std::endl;
        network::Socket::cleanup();
        return 1;
    }

    auto ghost_cache = std::make_unique<cache::GhostCache>();

    constexpr int kMaxSmokeFrames = 10;
    int frames = 0;
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

    std::cout << "Smoke test finished after " << frames << " frame(s)." << std::endl;
    std::cout << "  Active cache entries: " << ghost_cache->active_entries() << std::endl;

    ghost_cache->clear_all();
    window->destroy();
    network::Socket::cleanup();

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
        loader = std::make_shared<rendering::ResourceLoader>(cache,
            std::vector<std::shared_ptr<rendering::ResourceBackend>> { backend });
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
    node.children.clear();
    node.append_child(std::make_unique<rendering::DomNode>(rendering::DomNodeType::Text, "", text));
}

std::unique_ptr<rendering::DomNode> detach_child(rendering::DomNode& parent, rendering::DomNode* child)
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

// Sets one declaration in an element's style attribute, preserving the rest.
static void apply_style_property(rendering::DomNode* node, std::string const& property, std::string const& value)
{
    std::vector<std::pair<std::string, std::string>> declarations;
    if (auto* existing = node->attribute("style")) {
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
    }
    bool replaced = false;
    for (auto& entry : declarations)
        if (entry.first == property) {
            entry.second = value;
            replaced = true;
        }
    if (!replaced && !value.empty())
        declarations.emplace_back(property, value);
    std::string joined;
    for (auto const& entry : declarations) {
        if (entry.second.empty())
            continue;
        if (!joined.empty())
            joined += "; ";
        joined += entry.first + ": " + entry.second;
    }
    node->attributes["style"] = joined;
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
        node->attributes["value"] = value;
        return;
    }
    node->attributes["value"] = value;
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
        node->attributes["checked"] = "";
    else
        node->attributes.erase("checked");
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
    node->attributes[name] = raw ? raw : "";
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
    if (!node || argc < 2)
        return JS_UNDEFINED;
    const char* name = JS_ToCString(context, argv[0]);
    const char* value = JS_ToCString(context, argv[1]);
    if (name && value)
        node->attributes[name] = value;
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

void write_classes(rendering::DomNode* node, std::vector<std::string> const& classes)
{
    std::string joined;
    for (auto const& name : classes) {
        if (!joined.empty())
            joined += ' ';
        joined += name;
    }
    node->attributes["class"] = joined;
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
        for (auto& child : fragment.root().children)
            node->children.push_back(std::move(child));
    }
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
    if (property)
        apply_style_property(node, property, "");
    if (property)
        JS_FreeCString(context, property);
    return JS_UNDEFINED;
}

JSValue js_style_get(JSContext* context, JSValueConst this_value, int, JSValueConst*)
{
    auto* node = node_of(context, this_value);
    JSValue style = JS_NewObjectClass(context, g_dom_node_class);
    JS_SetOpaque(style, node);
    JS_SetPropertyStr(context, style, "setProperty", JS_NewCFunction(context, js_style_set_property, "setProperty", 2));
    JS_SetPropertyStr(context, style, "removeProperty", JS_NewCFunction(context, js_style_remove_property, "removeProperty", 1));
    return style;
}

JSValue js_style_set(JSContext* context, JSValueConst this_value, int argc, JSValueConst* argv)
{
    auto* node = node_of(context, this_value);
    if (!node || argc < 1)
        return JS_UNDEFINED;
    const char* raw = JS_ToCString(context, argv[0]);
    if (raw)
        node->attributes["style"] = raw;
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
        if (parent_it != parent_of.end())
            moved = detach_child(*parent_it->second, child);
        if (!moved)
            return;
    }
    parent->append_child(std::move(moved));
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

// Inline scripts run only when the policy permits them. No policy, or no
// script-src/default-src directive, means they are allowed.
static bool csp_allows_inline_scripts(std::string const& policy)
{
    if (policy.empty())
        return true;

    std::string script_source;
    bool have_script_source = false;
    std::string default_source;
    bool have_default_source = false;

    std::size_t start = 0;
    while (start <= policy.size()) {
        auto semicolon = policy.find(';', start);
        std::string directive = policy.substr(start,
            semicolon == std::string::npos ? std::string::npos : semicolon - start);
        start = semicolon == std::string::npos ? policy.size() + 1 : semicolon + 1;

        std::string name;
        std::string values;
        std::size_t index = 0;
        while (index < directive.size() && std::isspace(static_cast<unsigned char>(directive[index])))
            ++index;
        while (index < directive.size() && !std::isspace(static_cast<unsigned char>(directive[index])))
            name += static_cast<char>(std::tolower(static_cast<unsigned char>(directive[index++])));
        values = directive.substr(index);
        for (auto& c : values)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (name == "script-src") {
            script_source = values;
            have_script_source = true;
        } else if (name == "default-src") {
            default_source = values;
            have_default_source = true;
        }
    }

    std::string effective;
    if (have_script_source)
        effective = script_source;
    else if (have_default_source)
        effective = default_source;
    else
        return true; // no directive restricting scripts

    if (effective.find("'none'") != std::string::npos)
        return false;
    return effective.find("'unsafe-inline'") != std::string::npos;
}

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

static std::string chrome_document(std::string const& title, std::string const& address)
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
    std::string tab_title = escape(title.empty() ? address : title);
    if (tab_title.size() > 20)
        tab_title = tab_title.substr(0, 19) + "\u2026";

    // Absolutely positioned, not flex: the engine has no flexbox, and this
    // layout is a fixed 34px strip so explicit coordinates are exact.
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
    html += "<div style='" + box(8, 4, 190, 26, "#35363a", "6px") + "'></div>";
    html += label(16, 9, tab_title, "#e8eaed", 12);
    html += label(206, 8, "+", "#9aa0a6", 15);
    html += label(230, 8, "\u2190", "#9aa0a6", 14);
    html += label(256, 8, "\u2192", "#9aa0a6", 14);
    html += label(282, 8, "\u21bb", "#9aa0a6", 14);
    html += "<div style='" + box(310, 5, 660, 24, "#35363a", "12px") + "'></div>";
    html += label(320, 10, escape(address), "#e8eaed", 12);
    html += label(986, 8, "\u22ee", "#9aa0a6", 14);
    html += "</body></html>";
    return html;
}

// Runs the browser in a window: render, present, pump events. Without a display
// backend this still works headlessly for a fixed number of frames, which is
// what CI and the tests use.
static int run_window(std::string const& url, int frames, std::string const& frame_directory)
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

    auto page = rendering::RenderDocument::create(body, "", static_cast<float>(kWidth), nullptr, url);
    auto page_surface = page.render_to_surface(kWidth, content_height, rendering::Color { 255, 255, 255, 255 });

    std::string title = document_title(page.document().root());
    auto chrome = rendering::RenderDocument::create(chrome_document(title, url), "", static_cast<float>(kWidth));
    auto chrome_surface = chrome.render_to_surface(kWidth, kChromeHeight, rendering::Color { 32, 33, 36, 255 });

    auto window = rendering::SoftwareSurface(kWidth, kHeight, rendering::Color { 255, 255, 255, 255 });
    window.blit(chrome_surface, 0, 0);
    window.blit(page_surface, 0, kChromeHeight);

    auto host = ui::make_window_host(frames, frame_directory);
    if (!host->open(kWidth, kHeight, title.empty() ? "Aetheris" : title)) {
        std::cerr << "Could not open a window" << std::endl;
        return 1;
    }
    ui::WindowEvent event;
    int handled = 0;
    // Present, then drain input. The host closes itself when its frame budget is
    // spent (headless) or when the user closes the window.
    while (host->is_open()) {
        host->present(window);
        while (host->poll(event)) {
            if (event.kind == ui::WindowEvent::Kind::Close) {
                host->close();
                break;
            }
            if (event.kind == ui::WindowEvent::Kind::KeyDown) {
                if (event.key == "q" || event.key == "Escape") {
                    host->close();
                    break;
                }
                ++handled;
            }
        }
    }
    host->close();

    std::cout << "Window " << kWidth << "x" << kHeight << " (chrome " << kChromeHeight
              << "px, content " << content_height << "px), backend: "
              << (ui::has_display_backend() ? "display" : "headless")
              << ", events handled: " << handled << std::endl;
    return 0;
}

static int run_render_window(std::string const& url, std::string const& output_path)
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

    auto page = rendering::RenderDocument::create(body, "", static_cast<float>(kWidth), nullptr, url);
    auto page_surface = page.render_to_surface(kWidth, content_height, rendering::Color { 255, 255, 255, 255 });

    // The title comes from the page so the tab reads like a real browser.
    std::string title = document_title(page.document().root());

    auto chrome = rendering::RenderDocument::create(chrome_document(title, url), "", static_cast<float>(kWidth));
    auto chrome_surface = chrome.render_to_surface(kWidth, kChromeHeight, rendering::Color { 32, 33, 36, 255 });

    auto window = rendering::SoftwareSurface(kWidth, kHeight, rendering::Color { 255, 255, 255, 255 });
    window.blit(chrome_surface, 0, 0);
    window.blit(page_surface, 0, kChromeHeight);

    if (!rendering::ImageOutput::write_ppm(window, output_path)) {
        std::cerr << "Failed to write " << output_path << std::endl;
        return 1;
    }
    std::cout << "Rendered window " << kWidth << "x" << kHeight << " (chrome " << kChromeHeight
              << "px, content " << content_height << "px) to " << output_path << std::endl;
    return 0;
}

static int run_render(std::string const& url, std::string const& output_path, std::string const& dispatch_id = {}, std::string const& dispatch_type = {}, float click_x = -1, float click_y = -1) {
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
    for (auto const& dependency : loader.discover_dependencies(document_resource, base)) {
        std::string path = dependency.path();
        bool is_css = path.size() >= 4 && path.compare(path.size() - 4, 4, ".css") == 0;
        auto fetched = fetcher.fetch(is_css ? rendering::ResourceRequestContext::Style
                                            : rendering::ResourceRequestContext::Image,
            dependency);
        if (!fetched.succeeded())
            continue;
        ++fetched_count;
        std::string fetched_text(fetched.resource->bytes.begin(), fetched.resource->bytes.end());
        std::vector<unsigned char> bytes(fetched.resource->bytes.begin(), fetched.resource->bytes.end());
        if (is_css) {
            if (!css.empty())
                css.push_back('\n');
            css += fetched_text;
            resources.put({ rendering::ResourceType::Stylesheet, dependency.serialized(), std::move(bytes) });
        } else {
            resources.put({ rendering::ResourceType::Image, dependency.serialized(), std::move(bytes) });
        }
    }
    // Inline <style> blocks join the cascade after the linked stylesheets.
    for (auto const& block : extract_styles(document_body)) {
        if (!css.empty())
            css.push_back('\n');
        css += block;
    }

    network::Socket::cleanup();

    // Parse the document so inline scripts can read and mutate it.
    std::string html = document_body;
    rendering::Document document = rendering::Document::parse_html(html);

    rendering::CssParser computed_parser;
    rendering::CssStyleSheet computed_sheet = computed_parser.parse(css);
    g_stylesheet = &computed_sheet;

    std::string script_output;
    aetheris::js::Interpreter js;
    js.print_handler = [](std::string const& line) { std::cout << "  [js] " << line << std::endl; };
    js.write_handler = [&script_output](std::string const& text) { script_output += text; };

    g_js = &js;

    DomHost dom;
    dom.initialize(js.context());
    dom.collect(document.root());
    build_handles(document.root(), dom);

    // document / window surface
    {
        g_document_root = &document.root();
        auto* html = find_element(document.root(), "html");
        auto* body = find_element(document.root(), "body");
        auto* head = find_element(document.root(), "head");
        JSValue document_object = js.document_object();
        if (body)
            JS_SetPropertyStr(js.context(), document_object, "body", dom.handle_for(body));
        if (head)
            JS_SetPropertyStr(js.context(), document_object, "head", dom.handle_for(head));
        if (html)
            JS_SetPropertyStr(js.context(), document_object, "documentElement", dom.handle_for(html));

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

    // Content-Security-Policy: inline scripts only run when the policy allows.
    auto policy = content_security_policy(document.root());
    if (!csp_allows_inline_scripts(policy)) {
        std::cerr << "  [js] inline scripts blocked by Content-Security-Policy" << std::endl;
    } else for (auto const& script : extract_scripts(html)) {
        js.run(script);
        if (js.has_error()) {
            std::cerr << "  [js] " << js.error() << std::endl;
            break;
        }
    }
    if (!js.has_error()) {
        js.dispatch_event("DOMContentLoaded");
        js.dispatch_event("load");
        // Run timers (setTimeout/setInterval) on virtual time.
        js.run_event_loop();
    }
    if (!js.has_error() && !dispatch_id.empty() && !dispatch_type.empty()) {
        auto it = dom.handle_by_id.find(dispatch_id);
        auto* node = it != dom.handle_by_id.end()
            ? static_cast<rendering::DomNode*>(JS_GetOpaque(it->second, g_dom_node_class))
            : nullptr;
        if (node)
            dispatch_bubbling(js, dom, node, dispatch_type);
        else
            std::cerr << "  [js] no element with id '" << dispatch_id << "'" << std::endl;
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

    auto rendered = rendering::RenderDocument::create_from_document(std::move(document), css, 1024.0f, &resources, url);

    // Geometry-based dispatch: hit-test the point and fire a click on the
    // element there, then re-layout so the change is reflected.
    if (click_x >= 0 && click_y >= 0 && !js.has_error()) {
        rendering::DocumentInteraction interaction;
        rendering::Viewport viewport(1024.0f, 768.0f);
        viewport.set_document_size(1024.0f, 768.0f);
        auto hit = interaction.hit_test(rendered, viewport, click_x, click_y);
        auto* node = hit.dom_node ? const_cast<rendering::DomNode*>(hit.dom_node) : nullptr;
        while (node && dom.handle_by_node.find(node) == dom.handle_by_node.end()) {
            auto parent = dom.parent_of.find(node);
            node = parent != dom.parent_of.end() ? parent->second : nullptr;
        }
        if (node) {
            dispatch_bubbling(js, dom, node, "click");
            rendered.relayout();
        } else {
            std::cerr << "  [js] no element handle at the click point" << std::endl;
        }
    }
    // A PPM has no alpha channel, so render onto an opaque white background to
    // keep the output viewable.
    auto surface = rendered.render_to_surface(1024, 768, rendering::Color { 255, 255, 255, 255 });
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
            std::cout << "Aetheris Browser v0.3.0" << std::endl;
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
            return run_window(url, frames, frame_directory);
        }
        // Same page, but composited under the compact browser chrome.
        if (arg == "--render-window" && i + 1 < argc) {
            std::string url = argv[++i];
            std::string output_path = "aetheris-window.ppm";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                output_path = argv[++i];
            }
            return run_render_window(url, output_path);
        }
    }

    std::cout << "=== AETHERIS BROWSER v0.3.0 ===" << std::endl;
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

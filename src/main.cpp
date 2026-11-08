/**
 * AETHERIS BROWSER - MAIN ENTRY POINT
 * Cross-platform hyper-optimized browser
 */

#include "aetheris/core.hpp"
#include "aetheris/network/socket_abstraction.hpp"
#include "aetheris/network/http_client.hpp"
#include "aetheris/cache/ghost_cache.hpp"
#include "aetheris/ui/window_manager.hpp"

#include "render_document.hpp"
#include "image_output.hpp"
#include "resource_loader.hpp"
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
static void serialize_html(rendering::DomNode const& node, std::string& out);

// Prints the parsed DOM after scripts have run, so layout/DOM bugs can be
// inspected without rendering an image.
static int run_dump_dom(std::string const& url) {
    if (network::Socket::initialize() != network::SocketError::None) {
        std::cerr << "Failed to initialize network subsystem" << std::endl;
        return 1;
    }
    auto response = network::HttpClient::get(url);
    network::Socket::cleanup();

    if (!response.error.empty()) {
        std::cerr << "Fetch failed: " << response.error << std::endl;
        return 1;
    }

    auto document = rendering::Document::parse_html(response.body);
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

    auto response = network::HttpClient::get(url);
    network::Socket::cleanup();

    if (!response.error.empty()) {
        std::cerr << "Fetch failed: " << response.error << std::endl;
        return 1;
    }

    std::cout << response.status_line << std::endl;
    std::cout << "  Content-Type: " << response.header("Content-Type") << std::endl;
    std::cout << "  Bytes: " << response.body.size() << std::endl;
    return response.ok() ? 0 : 2;
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
    JSValue length_value = JS_GetPropertyStr(context, list, "length");
    uint32_t length = 0;
    JS_ToUint32(context, &length, length_value);
    JS_FreeValue(context, length_value);
    JS_SetPropertyUint32(context, list, length, JS_DupValue(context, argv[1]));
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
    JS_SetPropertyStr(context, handle, "addEventListener", JS_NewCFunction(context, js_element_add_event_listener, "addEventListener", 3));

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

// Links each element handle to its parent handle and its child elements.
static void wire_tree(DomHost& dom, rendering::DomNode& node, JSValue parent)
{
    JSValue current = JS_UNDEFINED;
    if (node.type == rendering::DomNodeType::Element) {
        auto it = dom.handle_by_node.find(&node);
        if (it != dom.handle_by_node.end())
            current = it->second;
    }
    if (!JS_IsUndefined(current)) {
        if (!JS_IsUndefined(parent))
            JS_SetPropertyStr(dom.context, current, "parentNode", JS_DupValue(dom.context, parent));
        JSValue children = JS_NewArray(dom.context);
        uint32_t index = 0;
        for (auto& child : node.children) {
            if (child->type != rendering::DomNodeType::Element)
                continue;
            auto it = dom.handle_by_node.find(child.get());
            if (it != dom.handle_by_node.end())
                JS_SetPropertyUint32(dom.context, children, index++, JS_DupValue(dom.context, it->second));
        }
        JS_SetPropertyStr(dom.context, current, "children", children);
    }
    for (auto& child : node.children)
        wire_tree(dom, *child, JS_IsUndefined(current) ? parent : current);
}

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
static int run_render(std::string const& url, std::string const& output_path, std::string const& dispatch_id = {}, std::string const& dispatch_type = {}, float click_x = -1, float click_y = -1) {
    if (network::Socket::initialize() != network::SocketError::None) {
        std::cerr << "Failed to initialize network subsystem" << std::endl;
        return 1;
    }

    auto response = network::HttpClient::get(url);
    if (!response.error.empty()) {
        network::Socket::cleanup();
        std::cerr << "Fetch failed: " << response.error << std::endl;
        return 1;
    }
    if (!response.ok()) {
        network::Socket::cleanup();
        std::cerr << "HTTP error: " << response.status_line << std::endl;
        return 2;
    }

    // Discover and fetch the document's linked resources: stylesheets are
    // concatenated into the cascade, images are cached for the engine to resolve.
    rendering::ResourceCache resources;
    rendering::ResourceLoader loader(resources);
    rendering::Url base = rendering::Url::parse(url);
    rendering::ResourceData document_resource {
        rendering::ResourceType::Document,
        url,
        std::vector<unsigned char>(response.body.begin(), response.body.end()),
    };

    std::string css;
    std::size_t fetched_count = 0;
    for (auto const& dependency : loader.discover_dependencies(document_resource, base)) {
        auto fetched = network::HttpClient::get(dependency.serialized());
        if (!fetched.error.empty() || !fetched.ok())
            continue;
        ++fetched_count;
        std::string path = dependency.path();
        bool is_css = path.size() >= 4 && path.compare(path.size() - 4, 4, ".css") == 0;
        std::vector<unsigned char> bytes(fetched.body.begin(), fetched.body.end());
        if (is_css) {
            if (!css.empty())
                css.push_back('\n');
            css += fetched.body;
            resources.put({ rendering::ResourceType::Stylesheet, dependency.serialized(), std::move(bytes) });
        } else {
            resources.put({ rendering::ResourceType::Image, dependency.serialized(), std::move(bytes) });
        }
    }
    // Inline <style> blocks join the cascade after the linked stylesheets.
    for (auto const& block : extract_styles(response.body)) {
        if (!css.empty())
            css.push_back('\n');
        css += block;
    }

    network::Socket::cleanup();

    // Parse the document so inline scripts can read and mutate it.
    std::string html = response.body;
    rendering::Document document = rendering::Document::parse_html(html);

    std::string script_output;
    aetheris::js::Interpreter js;
    js.print_handler = [](std::string const& line) { std::cout << "  [js] " << line << std::endl; };
    js.write_handler = [&script_output](std::string const& text) { script_output += text; };

    DomHost dom;
    dom.initialize(js.context());
    dom.collect(document.root());
    build_handles(document.root(), dom);
    wire_tree(dom, document.root(), JS_UNDEFINED);

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

    for (auto const& script : extract_scripts(html)) {
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

    std::cout << "Rendered " << response.body.size() << " bytes of HTML and "
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
            std::cout << "Aetheris Browser v0.2.0" << std::endl;
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
    }

    std::cout << "=== AETHERIS BROWSER v0.2.0 ===" << std::endl;
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

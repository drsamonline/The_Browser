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

// DOM helpers for the JavaScript host bindings.
static void collect_elements(rendering::DomNode& node, std::map<std::string, rendering::DomNode*>& out)
{
    if (node.type == rendering::DomNodeType::Element)
        if (auto* id = node.attribute("id"))
            out[*id] = &node;
    for (auto& child : node.children)
        collect_elements(*child, out);
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

// ---- JavaScript DOM host -----------------------------------------------------
// Owns the mapping between JS element handles and DOM nodes, including nodes
// created with document.createElement that are not yet in the tree.
namespace {

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

struct DomHost {
    std::map<aetheris::js::Object*, rendering::DomNode*> node_by_handle;
    std::map<rendering::DomNode*, std::shared_ptr<aetheris::js::Object>> handle_by_node;
    std::map<std::string, std::shared_ptr<aetheris::js::Object>> handle_by_id;
    std::map<rendering::DomNode*, rendering::DomNode*> parent_of;
    std::map<rendering::DomNode*, std::unique_ptr<rendering::DomNode>> pending;

    std::shared_ptr<aetheris::js::Object> handle_for(rendering::DomNode* node);

    void collect(rendering::DomNode& node)
    {
        for (auto& child : node.children) {
            parent_of[child.get()] = &node;
            collect(*child);
        }
    }

    void adopt(rendering::DomNode* parent, rendering::DomNode* child)
    {
        std::unique_ptr<rendering::DomNode> node;
        auto pit = pending.find(child);
        if (pit != pending.end()) {
            node = std::move(pit->second);
            pending.erase(pit);
        } else {
            auto par = parent_of.find(child);
            if (par != parent_of.end())
                node = detach_child(*par->second, child);
            if (!node)
                return;
        }
        parent->append_child(std::move(node));
        parent_of[child] = parent;
    }
};

std::shared_ptr<aetheris::js::Object> DomHost::handle_for(rendering::DomNode* node)
{
    auto handle = std::make_shared<aetheris::js::Object>();
    node_by_handle[handle.get()] = node;
    auto* id = node->attribute("id");
    handle->properties["id"] = aetheris::js::Value::from_string(id ? *id : "");
    handle->properties["textContent"] = aetheris::js::Value::from_string(node_text(*node));

    auto set_attribute = std::make_shared<aetheris::js::Object>();
    set_attribute->is_function = true;
    set_attribute->is_native = true;
    set_attribute->native = [node](std::vector<aetheris::js::Value> const& args) {
        if (args.size() >= 2)
            node->attributes[args[0].to_string()] = args[1].to_string();
        return aetheris::js::Value::undefined();
    };
    handle->properties["setAttribute"] = aetheris::js::Value::from_object(set_attribute);

    auto get_attribute = std::make_shared<aetheris::js::Object>();
    get_attribute->is_function = true;
    get_attribute->is_native = true;
    get_attribute->native = [node](std::vector<aetheris::js::Value> const& args) {
        if (args.empty())
            return aetheris::js::Value::undefined();
        auto* value = node->attribute(args[0].to_string());
        return value ? aetheris::js::Value::from_string(*value) : aetheris::js::Value::undefined();
    };
    handle->properties["getAttribute"] = aetheris::js::Value::from_object(get_attribute);

    auto* self = this;
    auto append_child = std::make_shared<aetheris::js::Object>();
    append_child->is_function = true;
    append_child->is_native = true;
    append_child->native = [self, node](std::vector<aetheris::js::Value> const& args) {
        if (!args.empty() && args[0].is_object() && args[0].object) {
            auto it = self->node_by_handle.find(args[0].object.get());
            if (it != self->node_by_handle.end())
                self->adopt(node, it->second);
        }
        return aetheris::js::Value::undefined();
    };
    handle->properties["appendChild"] = aetheris::js::Value::from_object(append_child);

    auto* raw_handle = handle.get();
    auto add_listener = std::make_shared<aetheris::js::Object>();
    add_listener->is_function = true;
    add_listener->is_native = true;
    add_listener->native = [raw_handle](std::vector<aetheris::js::Value> const& args) {
        if (args.size() < 2)
            return aetheris::js::Value::undefined();
        std::string key = "__handlers_" + args[0].to_string();
        auto it = raw_handle->properties.find(key);
        std::shared_ptr<aetheris::js::Object> array;
        if (it != raw_handle->properties.end() && it->second.is_object() && it->second.object) {
            array = it->second.object;
        } else {
            array = std::make_shared<aetheris::js::Object>();
            array->is_array = true;
            raw_handle->properties[key] = aetheris::js::Value::from_object(array);
        }
        array->elements.push_back(args[1]);
        return aetheris::js::Value::undefined();
    };
    handle->properties["addEventListener"] = aetheris::js::Value::from_object(add_listener);

    handle_by_node[node] = handle;
    if (id)
        handle_by_id[*id] = handle;

    return handle;
}

} // namespace

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
static int run_render(std::string const& url, std::string const& output_path, std::string const& dispatch_id = {}, std::string const& dispatch_type = {}) {
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
    network::Socket::cleanup();

    // Parse the document so inline scripts can read and mutate it.
    std::string html = response.body;
    rendering::Document document = rendering::Document::parse_html(html);

    DomHost dom;
    dom.collect(document.root());

    std::map<std::string, rendering::DomNode*> elements;
    collect_elements(document.root(), elements);

    std::vector<std::shared_ptr<aetheris::js::Object>> handles;
    std::map<std::string, std::shared_ptr<aetheris::js::Object>> handle_by_id;
    for (auto& [id, node] : elements) {
        auto handle = dom.handle_for(node);
        handle_by_id[id] = handle;
        handles.push_back(handle);
    }

    std::string script_output;
    {
        aetheris::js::Interpreter js;
        js.print_handler = [](std::string const& line) { std::cout << "  [js] " << line << std::endl; };
        js.write_handler = [&script_output](std::string const& text) { script_output += text; };
        js.element_lookup = [&handle_by_id](std::string const& id) -> aetheris::js::Value {
            auto it = handle_by_id.find(id);
            return it == handle_by_id.end() ? aetheris::js::Value::undefined() : aetheris::js::Value::from_object(it->second);
        };
        js.create_element = [&dom, &handles](std::string const& tag) -> aetheris::js::Value {
            auto node = std::make_unique<rendering::DomNode>(rendering::DomNodeType::Element, tag, "");
            auto* raw = node.get();
            dom.pending[raw] = std::move(node);
            auto handle = dom.handle_for(raw);
            handles.push_back(handle);
            return aetheris::js::Value::from_object(handle);
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
        }
        if (!js.has_error() && !dispatch_id.empty() && !dispatch_type.empty()) {
            auto it = dom.handle_by_id.find(dispatch_id);
            if (it != dom.handle_by_id.end())
                js.dispatch_event_to(aetheris::js::Value::from_object(it->second), dispatch_type);
            else
                std::cerr << "  [js] no element with id '" << dispatch_id << "'" << std::endl;
        }
    }

    // Apply textContent changes the scripts made back onto the DOM.
    for (auto& [handle, node] : dom.node_by_handle) {
        auto it = handle->properties.find("textContent");
        if (it == handle->properties.end())
            continue;
        std::string updated = it->second.to_string();
        if (updated != node_text(*node))
            set_node_text(*node, updated);
    }

    // document.write output is appended to the body.
    if (!script_output.empty()) {
        auto* body = find_element(document.root(), "body");
        auto text_node = std::make_unique<rendering::DomNode>(rendering::DomNodeType::Text, "", script_output);
        (body ? *body : document.root()).append_child(std::move(text_node));
    }

    auto rendered = rendering::RenderDocument::create_from_document(std::move(document), css, 1024.0f, &resources, url);
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
    return 0;
}

int main(int argc, char* argv[]) {
    using namespace std::chrono;

    // Optional element event to fire after a --render load.
    std::string dispatch_id;
    std::string dispatch_type;
    for (int i = 1; i + 2 < argc; ++i) {
        if (std::string(argv[i]) == "--dispatch") {
            dispatch_id = argv[i + 1];
            dispatch_type = argv[i + 2];
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
                      << "  --version      Print version information.\n"
                      << "  -h, --help     Print this help.\n";
            return 0;
        }
        if (arg == "--version" || arg == "-v") {
            std::cout << "Aetheris Browser v1.0.0" << std::endl;
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
        if (arg == "--render" && i + 1 < argc) {
            std::string url = argv[++i];
            std::string output_path = "aetheris-render.ppm";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                output_path = argv[++i];
            }
            return run_render(url, output_path, dispatch_id, dispatch_type);
        }
    }

    std::cout << "=== AETHERIS BROWSER v1.0.0 ===" << std::endl;
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

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

#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

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

// Headless render mode: fetch a URL, run the rendering pipeline and write the
// resulting surface to a PPM file. Exercises the full fetch -> parse -> style
// -> layout -> paint path without a display server.
static int run_render(std::string const& url, std::string const& output_path) {
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

    auto rendered = rendering::RenderDocument::create(response.body, css, 1024.0f, &resources, url);
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
        if (arg == "--render" && i + 1 < argc) {
            std::string url = argv[++i];
            std::string output_path = "aetheris-render.ppm";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                output_path = argv[++i];
            }
            return run_render(url, output_path);
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

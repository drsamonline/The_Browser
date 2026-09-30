/**
 * AETHERIS BROWSER - MAIN ENTRY POINT
 * Cross-platform hyper-optimized browser
 */

#include "aetheris/core.hpp"
#include "aetheris/network/socket_abstraction.hpp"
#include "aetheris/cache/ghost_cache.hpp"
#include "aetheris/ui/window_manager.hpp"

#include <chrono>
#include <cstdio>
#include <iostream>
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
static int run_smoke_test() {
    auto net_error = network::Socket::initialize();
    if (net_error != network::SocketError::None) {
        std::cerr << "Failed to initialize network subsystem" << std::endl;
        return 1;
    }

    ui::WindowManager window;
    ui::WindowConfig config;
    config.width = 1280;
    config.height = 720;
    config.title = "Aetheris Browser (smoke test)";

    if (!window.create(config)) {
        std::cerr << "Failed to create window" << std::endl;
        network::Socket::cleanup();
        return 1;
    }

    cache::GhostCache ghost_cache;

    constexpr int kMaxSmokeFrames = 10;
    int frames = 0;
    window.show();
    for (; frames < kMaxSmokeFrames; ++frames) {
        window.clear();
        window.draw_debug_overlay("AETHERIS SMOKE TEST");
        window.swap_buffers();
        if (!window.poll_events()) {
            break;
        }
    }

    std::cout << "Smoke test finished after " << frames << " frame(s)." << std::endl;
    std::cout << "  Active cache entries: " << ghost_cache.active_entries() << std::endl;

    ghost_cache.clear_all();
    window.destroy();
    network::Socket::cleanup();

    std::cout << "Aetheris smoke test passed." << std::endl;
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
    ui::WindowManager window;
    ui::WindowConfig config;
    config.width = 1280;
    config.height = 720;
    config.title = "Aetheris Browser";
    
    if (!window.create(config)) {
        std::cerr << "Failed to create window" << std::endl;
        network::Socket::cleanup();
        return 1;
    }
    
    // Create ghost cache
    cache::GhostCache ghost_cache;
    
    // Setup debug overlay
    DebugOverlay debug_overlay;
    
    // Event handler
    bool should_close = false;
    window.set_event_callback([&](ui::EventType type, const ui::KeyEvent& key, const ui::MouseEvent& mouse) {
        switch (type) {
            case ui::EventType::Close:
                should_close = true;
                break;
                
            case ui::EventType::KeyPress:
                if (key.is_pressed && key.keycode == 27) {  // ESC key
                    should_close = true;
                } else if (key.is_pressed && key.keycode == 122) {  // F11
                    window.toggle_fullscreen();
                }
                break;
                
            default:
                break;
        }
    });
    
    window.show();
    
    // Main loop timing
    auto last_time = steady_clock::now();
    double fps = 0.0;
    int frame_count = 0;
    auto fps_start = steady_clock::now();
    
    std::cout << "Window created successfully!" << std::endl;
    std::cout << "Starting main event loop..." << std::endl;
    std::cout << std::endl;
    
    // Main event loop
    while (!should_close) {
        // Poll events
        if (!window.poll_events()) {
            break;
        }
        
        // Clear and render
        window.clear();
        
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
                ghost_cache.total_compressed_bytes(),
                ghost_cache.active_entries(),
                ghost_cache.compression_ratio()
            );
        }
        
        // Draw debug overlay
        window.draw_debug_overlay(debug_overlay.get_text());
        
        window.swap_buffers();
        
        // Small delay to prevent CPU spinning (remove in production with proper vsync)
        std::this_thread::yield();
    }
    
    std::cout << std::endl;
    std::cout << "Shutting down..." << std::endl;
    std::cout << "Final cache statistics:" << std::endl;
    std::cout << "  Active entries: " << ghost_cache.active_entries() << std::endl;
    std::cout << "  Compressed bytes: " << ghost_cache.total_compressed_bytes() << std::endl;
    std::cout << "  Uncompressed bytes: " << ghost_cache.total_uncompressed_bytes() << std::endl;
    std::cout << "  Compression ratio: " << ghost_cache.compression_ratio() << "x" << std::endl;
    
    // Cleanup
    ghost_cache.clear_all();
    window.destroy();
    network::Socket::cleanup();
    
    std::cout << "Aetheris closed successfully." << std::endl;
    
    return 0;
}

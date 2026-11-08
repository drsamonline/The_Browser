#pragma once

#include <memory>
#include <string>

namespace aetheris::rendering {
class SoftwareSurface;
}

namespace aetheris::ui {

// Input the browser acts on. Deliberately small: a minimal browser needs
// pointing, typing, scrolling and a close request, nothing more.
struct WindowEvent {
    enum class Kind {
        None,
        Close,      // the window was closed
        KeyDown,    // `key` names the key ("l", "Escape", "F11", "Left", ...)
        TextInput,  // `text` holds the characters typed
        MouseDown,  // x, y, button
        MouseUp,
        MouseMove,  // x, y
        Scroll,     // x, y, delta
        Resize,     // width, height
    };

    Kind kind { Kind::None };
    std::string key;
    std::string text;
    int x { 0 };
    int y { 0 };
    int button { 0 };
    int delta { 0 };
    int width { 0 };
    int height { 0 };
    bool control { false };
    bool shift { false };
    bool alt { false };
};

// A window, a framebuffer and an event queue — nothing else. The point of the
// interface is that the browser chrome and input routing never learn which
// platform they are running on, so a native backend can replace SDL later
// without touching them.
class WindowHost {
public:
    virtual ~WindowHost() = default;

    virtual bool open(int width, int height, std::string const& title) = 0;
    virtual void present(rendering::SoftwareSurface const& surface) = 0;

    // False once the host is finished (window closed or frames exhausted).
    virtual bool poll(WindowEvent& event) = 0;

    virtual void set_title(std::string const& title) = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
};

// Renders frames without a display: useful for tests and for CI, where there is
// no window system. Writes each presented frame to a file when a directory is
// given, and can be told how many frames to accept before it reports closed.
class HeadlessWindowHost final : public WindowHost {
public:
    explicit HeadlessWindowHost(int frame_limit = 1, std::string output_directory = {});
    bool open(int width, int height, std::string const& title) override;
    void present(rendering::SoftwareSurface const& surface) override;
    bool poll(WindowEvent& event) override;
    void set_title(std::string const& title) override;
    void close() override;
    bool is_open() const override { return m_open; }

    int frames_presented() const { return m_frames; }
    std::string const& title() const { return m_title; }

private:
    int m_frame_limit { 1 };
    int m_frames { 0 };
    bool m_open { false };
    std::string m_output_directory;
    std::string m_title;
};

// Creates the platform window: SDL2 when the build has it, otherwise a headless
// host so the binary still runs everywhere.
std::unique_ptr<WindowHost> make_window_host(int frame_limit = 1, std::string output_directory = {});

// True when this build has a real window backend.
bool has_display_backend();

} // namespace aetheris::ui

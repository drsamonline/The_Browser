// SDL2 window backend.
//
// Compiled only when the build finds SDL2 (AETHERIS_WITH_SDL2). Everything the
// browser does with a window goes through WindowHost, so this file is the only
// place that knows SDL exists.
//
// Verified to compile against SDL 2.30 headers. It has not been run against a
// real display in this project's development environment; the headless host is
// what CI exercises. Set SDL_VIDEODRIVER=dummy to exercise it without a display.

#include "window_host.hpp"

#include "software_surface.hpp"

#include <SDL.h>

#include <string>

namespace aetheris::ui {

namespace {

// Our surface stores colours as 0xRRGGBBAA, which on a little-endian machine
// places the bytes in memory as A, B, G, R — SDL's ABGR8888.
constexpr Uint32 kTextureFormat = SDL_PIXELFORMAT_ABGR8888;

// SDL keysym to the names the input router expects.
std::string key_name(SDL_Keysym const& keysym)
{
    switch (keysym.sym) {
    case SDLK_ESCAPE:
        return "Escape";
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return "Enter";
    case SDLK_BACKSPACE:
        return "Backspace";
    case SDLK_LEFT:
        return "Left";
    case SDLK_RIGHT:
        return "Right";
    case SDLK_UP:
        return "Up";
    case SDLK_DOWN:
        return "Down";
    case SDLK_F11:
        return "F11";
    case SDLK_TAB:
        return "Tab";
    case SDLK_HOME:
        return "Home";
    case SDLK_END:
        return "End";
    default:
        break;
    }
    if (keysym.sym >= SDLK_a && keysym.sym <= SDLK_z)
        return std::string(1, static_cast<char>('a' + (keysym.sym - SDLK_a)));
    if (keysym.sym >= SDLK_0 && keysym.sym <= SDLK_9)
        return std::string(1, static_cast<char>('0' + (keysym.sym - SDLK_9 + 9)));
    return {};
}

} // namespace

class SdlWindowHost final : public WindowHost {
public:
    ~SdlWindowHost() override { close(); }

    bool open(int width, int height, std::string const& title) override
    {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            m_error = SDL_GetError();
            return false;
        }
        m_initialised = true;
        m_width = width;
        m_height = height;

        m_window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            width, height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        if (!m_window) {
            m_error = SDL_GetError();
            close();
            return false;
        }
        m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!m_renderer) {
            // Fall back to software rendering; a headless or remote session may
            // have no accelerated driver.
            m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_SOFTWARE);
        }
        if (!m_renderer) {
            m_error = SDL_GetError();
            close();
            return false;
        }
        m_texture = SDL_CreateTexture(m_renderer, kTextureFormat, SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!m_texture) {
            m_error = SDL_GetError();
            close();
            return false;
        }
        m_open = true;
        return true;
    }

    void present(rendering::SoftwareSurface const& surface) override
    {
        if (!m_open || !m_renderer)
            return;

        // The texture must match the surface exactly: SDL_UpdateTexture reads
        // width*height*4 bytes from the source, so a texture larger than the
        // surface reads past the buffer. A caller may present any size.
        if (!m_texture || surface.width() != m_width || surface.height() != m_height) {
            if (m_texture)
                SDL_DestroyTexture(m_texture);
            m_width = surface.width();
            m_height = surface.height();
            m_texture = SDL_CreateTexture(m_renderer, kTextureFormat, SDL_TEXTUREACCESS_STREAMING, m_width, m_height);
            if (!m_texture)
                return;
        }

        auto const& pixels = surface.pixels();
        SDL_UpdateTexture(m_texture, nullptr, pixels.data(),
            static_cast<int>(sizeof(uint32_t)) * surface.width());
        SDL_RenderClear(m_renderer);
        SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);
        SDL_RenderPresent(m_renderer);
    }

    bool poll(WindowEvent& event) override
    {
        event = WindowEvent {};
        if (!m_open)
            return false;
        SDL_Event raw;
        while (SDL_PollEvent(&raw)) {
            switch (raw.type) {
            case SDL_QUIT:
                event.kind = WindowEvent::Kind::Close;
                return true;
            case SDL_WINDOWEVENT:
                if (raw.window.event == SDL_WINDOWEVENT_CLOSE) {
                    event.kind = WindowEvent::Kind::Close;
                    return true;
                }
                if (raw.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    event.kind = WindowEvent::Kind::Resize;
                    event.width = raw.window.data1;
                    event.height = raw.window.data2;
                    return true;
                }
                break;
            case SDL_KEYDOWN:
                if (raw.key.repeat)
                    break;
                event.kind = WindowEvent::Kind::KeyDown;
                event.key = key_name(raw.key.keysym);
                event.control = (raw.key.keysym.mod & KMOD_CTRL) != 0;
                event.shift = (raw.key.keysym.mod & KMOD_SHIFT) != 0;
                event.alt = (raw.key.keysym.mod & KMOD_ALT) != 0;
                if (!event.key.empty())
                    return true;
                break;
            case SDL_TEXTINPUT:
                event.kind = WindowEvent::Kind::TextInput;
                event.text = raw.text.text;
                return true;
            case SDL_MOUSEBUTTONDOWN:
                event.kind = WindowEvent::Kind::MouseDown;
                event.x = raw.button.x;
                event.y = raw.button.y;
                event.button = raw.button.button;
                return true;
            case SDL_MOUSEBUTTONUP:
                event.kind = WindowEvent::Kind::MouseUp;
                event.x = raw.button.x;
                event.y = raw.button.y;
                event.button = raw.button.button;
                return true;
            case SDL_MOUSEMOTION:
                event.kind = WindowEvent::Kind::MouseMove;
                event.x = raw.motion.x;
                event.y = raw.motion.y;
                return true;
            case SDL_MOUSEWHEEL:
                event.kind = WindowEvent::Kind::Scroll;
                event.delta = raw.wheel.y;
                return true;
            default:
                break;
            }
        }
        return false;
    }

    void set_title(std::string const& title) override
    {
        if (m_window)
            SDL_SetWindowTitle(m_window, title.c_str());
    }

    void close() override
    {
        if (m_texture) {
            SDL_DestroyTexture(m_texture);
            m_texture = nullptr;
        }
        if (m_renderer) {
            SDL_DestroyRenderer(m_renderer);
            m_renderer = nullptr;
        }
        if (m_window) {
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
        }
        if (m_initialised) {
            SDL_Quit();
            m_initialised = false;
        }
        m_open = false;
    }

    bool is_open() const override { return m_open; }

    std::string const& error() const { return m_error; }

private:
    SDL_Window* m_window { nullptr };
    SDL_Renderer* m_renderer { nullptr };
    SDL_Texture* m_texture { nullptr };
    int m_width { 0 };
    int m_height { 0 };
    bool m_open { false };
    bool m_initialised { false };
    std::string m_error;
};

std::unique_ptr<WindowHost> make_sdl_window_host()
{
    return std::make_unique<SdlWindowHost>();
}

} // namespace aetheris::ui

#include "window_host.hpp"

#include "image_output.hpp"
#include "software_surface.hpp"

#include <cstdio>
#include <string>

namespace aetheris::ui {

HeadlessWindowHost::HeadlessWindowHost(int frame_limit, std::string output_directory)
    : m_frame_limit(frame_limit)
    , m_output_directory(std::move(output_directory))
{
}

bool HeadlessWindowHost::open(int, int, std::string const& title)
{
    m_title = title;
    m_open = true;
    m_frames = 0;
    return true;
}

void HeadlessWindowHost::present(rendering::SoftwareSurface const& surface)
{
    if (!m_open)
        return;
    ++m_frames;

    // Frames are only written when a directory was given, so a normal headless
    // run does not litter the working directory.
    if (!m_output_directory.empty()) {
        std::string path = m_output_directory + "/frame-" + std::to_string(m_frames) + ".ppm";
        rendering::ImageOutput::write_ppm(surface, path);
    }

    if (m_frames >= m_frame_limit)
        m_open = false;
}

bool HeadlessWindowHost::poll(WindowEvent& event)
{
    // No input in a headless host: poll never fills an event. Returning true
    // here with an empty event would spin the caller's loop forever.
    event = WindowEvent {};
    return false;
}

void HeadlessWindowHost::set_title(std::string const& title)
{
    m_title = title;
}

void HeadlessWindowHost::close()
{
    m_open = false;
}

std::unique_ptr<WindowHost> make_window_host(int frame_limit, std::string output_directory)
{
    // The SDL2 backend is compiled in only when the build found SDL2; until
    // then the headless host keeps the binary runnable everywhere.
    return std::make_unique<HeadlessWindowHost>(frame_limit, std::move(output_directory));
}

bool has_display_backend()
{
    return false;
}

} // namespace aetheris::ui

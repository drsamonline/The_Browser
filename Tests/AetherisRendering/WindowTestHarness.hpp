// A window is a browser, so a page's scripts must run in it, and a click must
// reach a handler. Both are checked by driving the real binary in --window mode
// against a local fixture.
//
// The exit code matters as much as the output: destroying the interpreter while
// the DOM host still held JS handles used to abort at teardown, after a perfectly
// good render.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

inline void require(bool condition, char const* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

struct RunResult {
    int status { -1 };
    std::string output;
};

// Serves `page` on loopback, runs the binary against it with `extra_arguments`,
// and returns its combined output and exit status. The server stops when the
// listener is shut down, never on an idle timeout: a loaded machine must not be
// able to lose it mid-run.
// Every frame directory a test asked for. Four window tests run in parallel, and a blanket
// `rm -rf /tmp/aetheris-*-frames` in each deleted the others' frames mid-run.
inline std::vector<std::string>& frame_directories()
{
    static std::vector<std::string> directories;
    return directories;
}

inline void cleanup_frames()
{
    for (auto const& directory : frame_directories())
        std::system(("rm -rf '" + directory + "'").c_str());
    frame_directories().clear();
}

inline RunResult run_window(std::string const& binary, std::string const& page,
    std::string const& extra_arguments, int frames = 2, std::string const& frame_directory = {},
    std::string const& second_page = {})
{
    int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    require(listener >= 0, "listener socket");
    int reuse = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    require(::bind(listener, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) == 0, "bind");
    require(::listen(listener, 8) == 0, "listen");
    socklen_t length = sizeof(address);
    require(::getsockname(listener, reinterpret_cast<struct sockaddr*>(&address), &length) == 0, "getsockname");
    uint16_t port = ntohs(address.sin_port);

    std::thread server([listener, page, second_page]() {
        for (;;) {
            int connection = ::accept(listener, nullptr, nullptr);
            if (connection < 0)
                break;
            char buffer[4096];
            auto received = ::recv(connection, buffer, sizeof(buffer), 0);
            // A second document, served for any path containing "/b", so a navigation can be tested.
            // The request line is "GET /b HTTP/1.1", and the body is chosen before the headers are sized.
            std::string request(buffer, received > 0 ? static_cast<std::size_t>(received) : 0);
            bool const wants_second = !second_page.empty() && request.find(" /b") != std::string::npos;
            std::string const& body = wants_second ? second_page : page;
            std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n";
            response += "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
            response += body;
            ::send(connection, response.data(), response.size(), 0);
            ::close(connection);
        }
        ::close(listener);
    });

    std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
    std::string command = "'" + binary + "' --window " + url + " " + std::to_string(frames);
    if (!frame_directory.empty()) {
        command += " " + frame_directory;
        frame_directories().push_back(frame_directory);
    }
    command += " " + extra_arguments + " 2>&1";
    FILE* pipe = ::popen(command.c_str(), "r");
    require(pipe != nullptr, "popen");
    RunResult result;
    char line[512];
    while (std::fgets(line, sizeof(line), pipe))
        result.output += line;
    result.status = ::pclose(pipe);

    ::shutdown(listener, SHUT_RDWR);
    ::close(listener);
    server.join();
    return result;
}

// Colour tests, written as what they mean. White is (255,255,255), so a check like
// `r > 200 && b > 200` matches white as well as magenta and quietly stops asserting
// anything - which cost a session on 2026-11-17.
inline bool is_magenta(int const (&rgb)[3]) { return rgb[0] > 200 && rgb[1] < 100 && rgb[2] > 200; }
inline bool is_red(int const (&rgb)[3]) { return rgb[0] > 200 && rgb[1] < 100 && rgb[2] < 100; }
inline bool is_green(int const (&rgb)[3]) { return rgb[0] < 100 && rgb[1] > 120 && rgb[2] < 100; }
inline bool is_blue(int const (&rgb)[3]) { return rgb[0] < 100 && rgb[1] < 100 && rgb[2] > 200; }

// Reads an integer count out of the window summary, or -1 when the label is missing,
// so a count that is not reported fails the assertion instead of matching nothing.
// The leading ", " matters: "layouts: " on its own also matches inside "relayouts: ".
inline int summary_count(std::string const& output, std::string const& label)
{
    auto at = output.find(label);
    if (at == std::string::npos)
        return -1;
    return std::atoi(output.c_str() + at + label.size());
}

// A frame the window wrote, decoded from its PPM.
struct Frame {
    int width { 0 };
    int height { 0 };
    std::vector<unsigned char> pixels;
};

// Reads the newest frame in `directory` (frames 1-8 are the ones the host keeps).
inline bool read_frame(std::string const& directory, Frame& frame)
{
    for (int index = 8; index >= 1; --index) {
        std::string path = directory + "/frame-" + std::to_string(index) + ".ppm";
        FILE* file = std::fopen(path.c_str(), "rb");
        if (!file)
            continue;
        char magic[3] = { 0, 0, 0 };
        int width = 0;
        int height = 0;
        int maximum = 0;
        bool ok = std::fscanf(file, "%2s %d %d %d", magic, &width, &height, &maximum) == 4
            && std::fgetc(file) == '\n' && width > 0 && height > 0;
        frame.width = width;
        frame.height = height;
        frame.pixels.clear();
        if (ok) {
            frame.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
            ok = std::fread(frame.pixels.data(), 1, frame.pixels.size(), file) == frame.pixels.size();
        }
        std::fclose(file);
        return ok;
    }
    return false;
}

// Whether two frames agree below the chrome strip. The chrome carries the URL, and the two runs
// are served on different ports, so it can never match and is not the subject.
inline bool frames_match_below_chrome(std::string const& left, std::string const& right, int chrome_height)
{
    Frame a;
    Frame b;
    if (!read_frame(left, a) || !read_frame(right, b))
        return false;
    if (a.width != b.width || a.height != b.height)
        return false;
    auto const row_bytes = static_cast<std::size_t>(a.width) * 3;
    for (int y = chrome_height; y < a.height; ++y) {
        auto const offset = static_cast<std::size_t>(y) * row_bytes;
        if (std::memcmp(a.pixels.data() + offset, b.pixels.data() + offset, row_bytes) != 0)
            return false;
    }
    return true;
}

// Reads one pixel out of the last frame the window wrote. Returns false when the
// frame is missing or malformed, so a broken writer fails loudly.
inline bool frame_pixel(std::string const& directory, int x, int y, int (&rgb)[3])
{
    for (int index = 8; index >= 1; --index) {
        std::string path = directory + "/frame-" + std::to_string(index) + ".ppm";
        FILE* file = std::fopen(path.c_str(), "rb");
        if (!file)
            continue;
        char magic[3] = { 0, 0, 0 };
        int width = 0;
        int height = 0;
        int maximum = 0;
        bool ok = std::fscanf(file, "%2s %d %d %d", magic, &width, &height, &maximum) == 4
            && std::fgetc(file) == '\n' && x < width && y < height;
        if (ok) {
            long offset = static_cast<long>(y) * width * 3 + static_cast<long>(x) * 3;
            ok = std::fseek(file, offset, SEEK_CUR) == 0;
            unsigned char bytes[3] = { 0, 0, 0 };
            ok = ok && std::fread(bytes, 1, 3, file) == 3;
            rgb[0] = bytes[0];
            rgb[1] = bytes[1];
            rgb[2] = bytes[2];
        }
        std::fclose(file);
        return ok;
    }
    return false;
}

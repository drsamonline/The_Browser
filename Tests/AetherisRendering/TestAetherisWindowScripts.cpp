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

static void require(bool condition, char const* message)
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
static RunResult run_window(std::string const& binary, std::string const& page,
    std::string const& extra_arguments, int frames = 2, std::string const& frame_directory = {})
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

    std::thread server([listener, page]() {
        for (;;) {
            int connection = ::accept(listener, nullptr, nullptr);
            if (connection < 0)
                break;
            char buffer[4096];
            ::recv(connection, buffer, sizeof(buffer), 0);
            std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n";
            response += "Content-Length: " + std::to_string(page.size()) + "\r\nConnection: close\r\n\r\n";
            response += page;
            ::send(connection, response.data(), response.size(), 0);
            ::close(connection);
        }
        ::close(listener);
    });

    std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
    std::string command = "'" + binary + "' --window " + url + " " + std::to_string(frames);
    if (!frame_directory.empty())
        command += " " + frame_directory;
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
static bool is_magenta(int const (&rgb)[3]) { return rgb[0] > 200 && rgb[1] < 100 && rgb[2] > 200; }
static bool is_red(int const (&rgb)[3]) { return rgb[0] > 200 && rgb[1] < 100 && rgb[2] < 100; }
static bool is_green(int const (&rgb)[3]) { return rgb[0] < 100 && rgb[1] > 120 && rgb[2] < 100; }
static bool is_blue(int const (&rgb)[3]) { return rgb[0] < 100 && rgb[1] < 100 && rgb[2] > 200; }

// Reads an integer count out of the window summary, or -1 when the label is missing,
// so a count that is not reported fails the assertion instead of matching nothing.
// The leading ", " matters: "layouts: " on its own also matches inside "relayouts: ".
static int summary_count(std::string const& output, std::string const& label)
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
static bool read_frame(std::string const& directory, Frame& frame)
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
static bool frames_match_below_chrome(std::string const& left, std::string const& right, int chrome_height)
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
static bool frame_pixel(std::string const& directory, int x, int y, int (&rgb)[3])
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

int main(int argc, char* argv[])
{
    require(argc >= 2, "browser binary path argument");
    std::string binary = argv[1];

    // Scripts run at load.
    {
        auto result = run_window(binary,
            "<html><head><title>Scripted</title></head><body>"
            "<div id='host'>before</div>"
            "<script>console.log('MARK window script ran');"
            "document.getElementById('host').textContent = 'built by script';</script>"
            "</body></html>",
            "");
        require(result.status == 0, "the window run exits cleanly");
        require(result.output.find("MARK window script ran") != std::string::npos,
            "scripts run in the window loop");
        require(result.output.find("Window 1024x768") != std::string::npos, "the window loop completed");
    }

    // A click reaches an element handler, and the resulting DOM change is laid
    // out. The button is at the top of the content area, so window y 80 is
    // inside it once the 34 px chrome strip is subtracted.
    {
        auto result = run_window(binary,
            "<html><head><title>Click</title>"
            "<style>body{margin:0}button{width:400px;height:100px}</style></head><body>"
            "<button id='b'>Press me</button>"
            "<script>document.getElementById('b').addEventListener('click', function () {"
            "console.log('MARK button clicked');"
            "var d = document.createElement('div'); d.textContent = 'added'; document.body.appendChild(d);"
            "});</script></body></html>",
            "--click 200 80");
        require(result.status == 0, "the clicked run exits cleanly");
        require(result.output.find("MARK button clicked") != std::string::npos,
            "a click reaches the element's handler");
    }

    // A repeating timer fires once per frame's worth of virtual time, not a
    // thousand times at load and not never again. 20 frames at 16 ms each is
    // 320 ms of virtual time, so a 50 ms interval should tick a handful of times.
    {
        auto result = run_window(binary,
            "<html><head><title>Tick</title></head><body><div id='n'>0</div>"
            "<script>var n = 0;"
            "setInterval(function () { n++; document.getElementById('n').textContent = String(n);"
            "console.log('TICK', n); }, 50);</script></body></html>",
            "", 20);
        require(result.status == 0, "the timer run exits cleanly");
        int ticks = 0;
        std::size_t at = 0;
        while ((at = result.output.find("TICK ", at)) != std::string::npos) {
            ++ticks;
            at += 5;
        }
        require(ticks >= 2, "a repeating timer fires more than once");
        require(ticks < 100, "a repeating timer does not drain the whole queue at load");
    }

    // An idle timer must not cost a layout per frame; a timer that changes the
    // document must. The window reports its relayout count so this is observable
    // from outside rather than taken on trust.
    {
        auto idle = run_window(binary,
            "<html><head><title>Idle</title></head><body><div id='n'>0</div>"
            "<script>var n = 0; setInterval(function () { n++; }, 50);</script></body></html>",
            "", 20);
        require(idle.status == 0, "the idle timer run exits cleanly");
        require(idle.output.find("relayouts: 0") != std::string::npos,
            "a timer that changes nothing costs no layout");

        auto busy = run_window(binary,
            "<html><head><title>Busy</title></head><body><div id='n'>0</div>"
            "<script>var n = 0; setInterval(function () { n++;"
            "document.getElementById('n').textContent = String(n); }, 50);</script></body></html>",
            "", 20);
        require(busy.status == 0, "the mutating timer run exits cleanly");
        require(busy.output.find("relayouts: 0") == std::string::npos,
            "a timer that changes the document triggers a layout");
    }

    // A key reaches the page's keyboard handler, with its value intact, and the
    // handler's DOM change is laid out.
    {
        auto result = run_window(binary,
            "<html><head><title>Keys</title></head><body><div id='out'>none</div>"
            "<script>document.addEventListener('keydown', function (e) {"
            "console.log('MARK key', e.key);"
            "document.getElementById('out').textContent = 'saw ' + e.key; });</script></body></html>",
            "--key j");
        require(result.status == 0, "the key run exits cleanly");
        require(result.output.find("MARK key j") != std::string::npos,
            "a key reaches the page with its value");
        require(result.output.find("relayouts: 0") == std::string::npos,
            "a key handler that changes the document triggers a layout");
    }

    // Hover reaches the element under the pointer.
    {
        auto result = run_window(binary,
            "<html><head><title>Hover</title>"
            "<style>body{margin:0}div{width:400px;height:100px}</style></head><body>"
            "<div id='a'>A</div><div id='b'>B</div>"
            "<script>document.getElementById('a').addEventListener('mouseover', function () {"
            "console.log('MARK over a'); });</script></body></html>",
            "--move 200 80");
        require(result.status == 0, "the hover run exits cleanly");
        require(result.output.find("MARK over a") != std::string::npos,
            "hover reaches the element under the pointer");
    }

    // CSS :hover reaches the pixels, not just the JavaScript handlers. The
    // fixture paints both divs blue and turns the first red on hover, so the
    // frame itself is the assertion.
    {
        std::string directory = "/tmp/aetheris-hover-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto result = run_window(binary,
            "<html><head><title>Hover CSS</title><style>body{margin:0}"
            "div{width:400px;height:100px;background:#0000ff}#a:hover{background:#ff0000}"
            "</style></head><body><div id='a'>A</div><div id='b'>B</div></body></html>",
            "--move 200 80", 2, directory);
        require(result.status == 0, "the hover css run exits cleanly");

        // The last frame written is the one after the move.
        std::string frame = directory + "/frame-2.ppm";
        FILE* file = std::fopen(frame.c_str(), "rb");
        if (!file)
            file = std::fopen((directory + "/frame-1.ppm").c_str(), "rb");
        require(file != nullptr, "the window wrote a frame");

        char magic[3] = { 0, 0, 0 };
        int width = 0;
        int height = 0;
        int maximum = 0;
        require(std::fscanf(file, "%2s %d %d %d", magic, &width, &height, &maximum) == 4,
            "the frame has a PPM header");
        require(std::fgetc(file) == '\n', "the PPM header ends");
        require(width == 1024 && height == 768, "the frame is window sized");

        std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 3);
        require(std::fread(pixels.data(), 1, pixels.size(), file) == pixels.size(), "the frame is complete");
        std::fclose(file);

        auto at = [&](int x, int y) {
            auto index = (static_cast<std::size_t>(y) * width + x) * 3;
            return std::array<int, 3> { pixels[index], pixels[index + 1], pixels[index + 2] };
        };
        // 34 px of chrome, then the first div occupies rows 0-99 of the content.
        auto hovered = at(200, 34 + 50);
        auto plain = at(200, 34 + 150);
        require(hovered[0] > 200 && hovered[1] < 60, ":hover turned the hovered element red");
        require(plain[2] > 200 && plain[0] < 60, "the element without :hover stayed blue");
    }

    // CSS :active shows while the button is down. --press leaves it pressed, so
    // the frame it produces is the only way to see this without a display.
    {
        std::string directory = "/tmp/aetheris-active-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto result = run_window(binary,
            "<html><head><title>Active</title><style>body{margin:0}"
            "div{width:400px;height:100px;background:#0000ff}#a:active{background:#00ff00}"
            "</style></head><body><div id='a'>A</div><div id='b'>B</div></body></html>",
            "--press 200 80", 2, directory);
        require(result.status == 0, "the active run exits cleanly");

        int pressed[3] = { 0, 0, 0 };
        int other[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 200, 34 + 50, pressed), "the pressed frame is readable");
        require(frame_pixel(directory, 200, 34 + 150, other), "the other pixel is readable");
        require(pressed[1] > 200 && pressed[0] < 60, ":active turned the pressed element green");
        require(other[2] > 200 && other[0] < 60, "the element without :active stayed blue");
    }

    // Tab moves focus, and the focus ring appears. Both halves matter: the focus
    // state can be right and the frame still never redrawn, which is exactly how
    // :focus-visible stayed "broken" for two sessions.
    {
        std::string directory = "/tmp/aetheris-tab-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto result = run_window(binary,
            "<html><head><title>Tab</title><style>body{margin:0}"
            "#a{width:400px;height:100px;background:#0000ff}"
            "#a:focus-visible{background:#ff00ff}"
            "</style></head><body><div id='a' tabindex='0'>A</div></body></html>",
            "--key tab", 3, directory);
        require(result.status == 0, "the tab run exits cleanly");

        // The last frame is the one after the Tab. If focus moved but nothing asked
        // for a repaint, this stays blue and the test fails - which is the whole
        // point: the state can be correct while the frame is stale.
        int after[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 200, 34 + 50, after), "the frame after Tab is readable");
        require(is_magenta(after), ":focus-visible paints after Tab (magenta)");
    }

    // The mutation counter must see more than textContent: an attribute write is
    // also a document change, and an under-counting counter produces a stale frame.
    {
        auto result = run_window(binary,
            "<html><head><title>Attr</title></head><body><div id='n'>0</div>"
            "<script>var n = 0; setInterval(function () { n++;"
            "document.getElementById('n').setAttribute('class', 'c' + n); }, 50);</script></body></html>",
            "", 20);
        require(result.status == 0, "the attribute timer run exits cleanly");
        require(result.output.find("relayouts: 0") == std::string::npos,
            "a class write triggers a layout");
    }

    // :focus-within matches an ancestor of the focused element, including when the
    // focus arrived by Tab rather than a click - the two paths set the state through
    // different helpers.
    {
        std::string directory = "/tmp/aetheris-focus-within-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto result = run_window(binary,
            "<html><head><title>FW</title><style>body{margin:0}"
            "#outer{width:400px;height:200px;background:#0000ff}"
            "#inner{width:200px;height:100px;background:#00aa00}"
            "#outer:focus-within{background:#ff0000}"
            "</style></head><body><div id='outer'><div id='inner' tabindex='0'>in</div></div></body></html>",
            "--key tab", 3, directory);
        require(result.status == 0, "the focus-within run exits cleanly");

        int outer[3] = { 0, 0, 0 };
        int inner[3] = { 0, 0, 0 };
        // 300,150 is inside #outer but outside #inner; 100,50 is inside #inner.
        require(frame_pixel(directory, 300, 34 + 150, outer), "the outer pixel is readable");
        require(frame_pixel(directory, 100, 34 + 50, inner), "the inner pixel is readable");
        require(is_red(outer), ":focus-within paints the ancestor after Tab");
        require(is_green(inner), "the focused child keeps its own background");
    }

    // :focus-visible's text-entry clause: a control indicates focus even when the
    // focus arrived by pointer, which for anything else is exactly when it does not.
    // Both halves are asserted, and "magenta" is tested as magenta (green near zero),
    // not as "not white" - white is (255,255,255), so a red-and-blue test matches it
    // and the negative assertion silently becomes meaningless.
    {
        std::string html =
            "<html><head><title>TE</title><style>body{margin:0}"
            "#i:focus-visible{background:#ff00ff}"
            "</style></head><body><input id='i' value='hello'></body></html>";

        std::string directory = "/tmp/aetheris-text-entry-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto clicked = run_window(binary, html, "--click 60 42", 3, directory);
        require(clicked.status == 0, "the text-entry click run exits cleanly");
        int on_field[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 60, 34 + 8, on_field), "the field pixel is readable");
        require(is_magenta(on_field), "a pointer-focused input matches :focus-visible (text-entry clause)");

        directory = "/tmp/aetheris-text-entry-away-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto away = run_window(binary, html, "--click 600 300", 3, directory);
        require(away.status == 0, "the click-away run exits cleanly");
        int off_field[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 60, 34 + 8, off_field), "the control pixel is readable");
        require(!is_magenta(off_field), "a pointer focus elsewhere does not match :focus-visible");
    }

    // The pseudo-class gate: a state change that no rule can observe must not relayout.
    // Without a :hover rule, moving the pointer over the page costs nothing.
    {
        auto result = run_window(binary,
            "<html><head><title>Gate</title><style>body{margin:0}"
            "div{width:400px;height:100px;background:#0000ff}"
            "</style></head><body><div id='a'>A</div></body></html>",
            "--move 200 80", 3);
        require(result.status == 0, "the gate run exits cleanly");
        require(result.output.find("relayouts: 0") != std::string::npos,
            "a hover with no :hover rule costs no relayout");
    }

    // ...and with a rule that can see it, it does relayout.
    {
        auto result = run_window(binary,
            "<html><head><title>Gate</title><style>body{margin:0}"
            "div{width:400px;height:100px;background:#0000ff}"
            "#a:hover{background:#ff00ff}"
            "</style></head><body><div id='a'>A</div></body></html>",
            "--move 200 80", 3);
        require(result.status == 0, "the gate run with a rule exits cleanly");
        require(result.output.find("relayouts: 0") == std::string::npos,
            "a hover with a :hover rule does relayout");
    }

    // Attribute writes are gated the same way: an attribute no selector mentions and
    // the engine does not read cannot change layout or paint.
    {
        auto data_attr = run_window(binary,
            "<html><body><div id='n'>0</div><script>var n = 0;"
            "setInterval(function () { n++; document.getElementById('n').setAttribute('data-n', String(n)); }, 50);"
            "</script></body></html>", "", 20);
        require(data_attr.status == 0, "the data-attribute run exits cleanly");
        require(data_attr.output.find("relayouts: 0") != std::string::npos,
            "an unobserved attribute write costs no relayout");

        auto class_attr = run_window(binary,
            "<html><body><div id='n'>0</div><script>var n = 0;"
            "setInterval(function () { n++; document.getElementById('n').setAttribute('class', 'c' + n); }, 50);"
            "</script></body></html>", "", 20);
        require(class_attr.status == 0, "the class-attribute run exits cleanly");
        require(class_attr.output.find("relayouts: 0") == std::string::npos,
            "a class write does relayout");
    }

    // Attribute selectors, both forms, and the gate that depends on them: a write to an
    // attribute that IS observed must relayout, which is the case the gate alone would
    // have skipped if uses_attribute had stayed a stub.
    {
        std::system("rm -rf /tmp/aetheris-attr-frames && mkdir -p /tmp/aetheris-attr-frames");
        auto styled = run_window(binary,
            "<html><head><style>body{margin:0}div{width:400px;height:100px;background:#0000ff}"
            "[data-x]{background:#ff00ff}[data-y=one]{background:#00ff00}</style></head><body>"
            "<div id='a' data-x='1'>a</div><div id='b'>b</div><div id='c' data-y='one'>c</div>"
            "<div id='d' data-y='two'>d</div></body></html>", "", 2, "/tmp/aetheris-attr-frames");
        require(styled.status == 0, "the attribute selector run exits cleanly");

        int present[3] = { 0, 0, 0 };
        int absent[3] = { 0, 0, 0 };
        int equal[3] = { 0, 0, 0 };
        int differs[3] = { 0, 0, 0 };
        require(frame_pixel("/tmp/aetheris-attr-frames", 200, 34 + 50, present), "attr pixel 1");
        require(frame_pixel("/tmp/aetheris-attr-frames", 200, 34 + 150, absent), "attr pixel 2");
        require(frame_pixel("/tmp/aetheris-attr-frames", 200, 34 + 250, equal), "attr pixel 3");
        require(frame_pixel("/tmp/aetheris-attr-frames", 200, 34 + 350, differs), "attr pixel 4");
        require(is_magenta(present), "[data-x] matches an element that has it");
        require(is_blue(absent), "[data-x] does not match an element without it");
        require(is_green(equal), "[data-y=one] matches the value one");
        require(is_blue(differs), "[data-y=one] does not match the value two");

        auto observed = run_window(binary,
            "<html><head><style>[data-x]{background:#ff00ff}</style></head><body>"
            "<div id='n' data-x='0'>0</div><script>var n = 0;"
            "setInterval(function () { n++; document.getElementById('n').setAttribute('data-x', String(n)); }, 50);"
            "</script></body></html>", "", 20);
        require(observed.status == 0, "the observed attribute run exits cleanly");
        require(observed.output.find("relayouts: 0") == std::string::npos,
            "a write to an attribute a selector mentions does relayout");
    }

    // The five attribute operators, each asserted in both directions. A single page,
    // one row per operator: magenta where it should match, blue where it should not.
    {
        std::string directory = "/tmp/aetheris-attr-ops-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto result = run_window(binary,
            "<html><head><style>body{margin:0}div{width:400px;height:60px;background:#0000ff}"
            "[class~=note]{background:#ff00ff}[lang|=en]{background:#ff00ff}"
            "[href^=https]{background:#ff00ff}[href$=.png]{background:#ff00ff}"
            "[href*=mid]{background:#ff00ff}</style></head><body>"
            "<div id='a' class='x note y'>1</div>"
            "<div id='b' class='x noted y'>2</div>"
            "<div id='c' lang='en-GB'>3</div>"
            "<div id='d' lang='eng'>4</div>"
            "<div id='e' href='https://a'>5</div>"
            "<div id='f' href='http://a'>6</div>"
            "<div id='g' href='/a.png'>7</div>"
            "<div id='h' href='/a.pngz'>8</div>"
            "<div id='i' href='/a-mid-b'>9</div>"
            "<div id='j' href='/a-b'>10</div>"
            "</body></html>", "", 2, directory);
        require(result.status == 0, "the attribute operator run exits cleanly");

        struct Row { int y; bool should_match; char const* what; };
        Row const rows[] = {
            { 0, true, "[class~=note] matches a word" },
            { 60, false, "[class~=note] does not match a prefix" },
            { 120, true, "[lang|=en] matches en-GB" },
            { 180, false, "[lang|=en] does not match eng" },
            { 240, true, "[href^=https] matches" },
            { 300, false, "[href^=https] does not match http" },
            { 360, true, "[href$=.png] matches" },
            { 420, false, "[href$=.png] does not match .pngz" },
            { 480, true, "[href*=mid] matches" },
            { 540, false, "[href*=mid] does not match" },
        };
        for (auto const& row : rows) {
            int rgb[3] = { 0, 0, 0 };
            require(frame_pixel(directory, 200, 34 + row.y + 30, rgb), "operator row readable");
            if (row.should_match)
                require(is_magenta(rgb), row.what);
            else
                require(is_blue(rgb), row.what);
        }
    }

    // The style cache: a text write deep in a 200-node page must resolve only the
    // affected branch, and a class write must still repaint - a cache that serves a
    // stale style is the bug this has to not have.
    {
        std::string rows;
        for (int index = 0; index < 200; ++index)
            rows += "<div class='r'>row " + std::to_string(index) + "</div>";

        auto many = run_window(binary,
            "<html><head><style>body{margin:0}div{height:3px;background:#0000ff}</style></head><body>"
            "<div id='target'>target</div>" + rows +
            "<script>var n = 0; setInterval(function () { n++;"
            "document.getElementById('target').textContent = 'target ' + n; }, 50);</script></body></html>",
            "", 20);
        require(many.status == 0, "the cache run exits cleanly");
        auto resolves_at = many.output.find("resolves: ");
        require(resolves_at != std::string::npos, "the summary reports resolves");
        int resolved = std::atoi(many.output.c_str() + resolves_at + 10);
        require(resolved > 0 && resolved < 40,
            "a text write in a 200-node page resolves a handful of nodes, not all of them");
        require(many.output.find("relayouts: 0") == std::string::npos,
            "and it still relayouts, so the change is not dropped");

        // Correctness: a class write that changes the background must repaint.
        std::string directory = "/tmp/aetheris-cache-class-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto repaint = run_window(binary,
            "<html><head><style>body{margin:0}div{width:400px;height:100px;background:#0000ff}"
            ".hot{background:#ff00ff}</style></head><body><div id='a'>A</div>"
            "<script>setTimeout(function () { document.getElementById('a').setAttribute('class', 'hot'); }, 5);"
            "</script></body></html>", "", 20, directory);
        require(repaint.status == 0, "the class repaint run exits cleanly");
        int painted[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 200, 34 + 50, painted), "the repaint pixel is readable");
        require(is_magenta(painted), "a class change repaints through the cache");
    }

    // Incremental layout: an attribute write that can only change paint must rebuild
    // the render tree without running the layout engine. The summary reports both
    // counts, so "laid out" and "repainted" are told apart from outside the process.
    {
        // Background only, written on every tick: six relayouts, one layout - the one
        // the page loaded with.
        auto paint = run_window(binary,
            "<html><head><title>Incremental</title><style>body{margin:0}"
            "div{width:400px;height:100px;background:#0000ff}.on{background:#ff00ff}"
            "</style></head><body><div id='a'>a</div><script>"
            "setInterval(function () { document.getElementById('a').setAttribute('class', 'on'); }, 50);"
            "</script></body></html>", "", 20);
        require(paint.status == 0, "the paint-only run exits cleanly");
        require(summary_count(paint.output, ", relayouts: ") == 6,
            "a background-only class write relayouts on every tick");
        require(summary_count(paint.output, ", layouts: ") == 1,
            "a background-only class write never runs the layout engine");

        // A text write can reflow, so every relayout of one runs the engine. This is
        // the other direction of the same claim, and it must be able to come out the
        // other way or the first assertion proves nothing.
        auto text = run_window(binary,
            "<html><head><title>Incremental text</title></head><body><div id='n'>0</div><script>var n = 0;"
            "setInterval(function () { n++; document.getElementById('n').textContent = String(n); }, 50);"
            "</script></body></html>", "", 20);
        require(text.status == 0, "the text-write run exits cleanly");
        require(summary_count(text.output, ", relayouts: ") == 6,
            "a text write relayouts on every tick");
        require(summary_count(text.output, ", layouts: ") == summary_count(text.output, ", relayouts: ") + 1,
            "a text write runs the layout engine on every relayout");

        // The control: the same attribute write, changing width instead of background.
        // If this also skipped, the skip would be unconditional and the two runs above
        // would agree by accident.
        auto width = run_window(binary,
            "<html><head><title>Incremental width</title><style>body{margin:0}"
            "div{width:400px;height:100px;background:#0000ff}.on{width:200px}"
            "</style></head><body><div id='a'>a</div><script>var n = 0;"
            "setInterval(function () { n++; document.getElementById('a').setAttribute('class', n === 1 ? 'on' : ''); }, 50);"
            "</script></body></html>", "", 20);
        require(width.status == 0, "the width-change run exits cleanly");
        require(summary_count(width.output, ", relayouts: ") == 6,
            "a width-changing class write relayouts on every tick");
        require(summary_count(width.output, ", layouts: ") > 1,
            "a class write that changes width does run the layout engine");

        // And the skipped layout must not skip the paint. The frame is read, not
        // inferred: a stale frame after a skipped layout is the bug this project has
        // shipped twice.
        // Under the build tree, not /tmp: this sandbox's /tmp is a 64 MB tmpfs and the
        // suite's other frame dumps already fill it, which truncated these frames to
        // zero bytes and failed this assertion for a reason that had nothing to do with
        // layout. The frame is 1024x768x3, so eight of them are 19 MB.
        std::string directory = "aetheris-incremental-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto repaint = run_window(binary,
            "<html><head><title>Incremental paint</title><style>body{margin:0}"
            "div{width:400px;height:100px;background:#0000ff}.on{background:#ff00ff}"
            "</style></head><body><div id='a'>A</div><script>"
            "setTimeout(function () { document.getElementById('a').setAttribute('class', 'on'); }, 5);"
            "</script></body></html>", "", 2, directory);
        require(repaint.status == 0, "the skipped-layout repaint run exits cleanly");
        require(summary_count(repaint.output, ", relayouts: ") == 1,
            "the paint-only change relayouts once");
        require(summary_count(repaint.output, ", layouts: ") == 1,
            "and does not run the layout engine");
        int painted[3] = { 0, 0, 0 };
        int outside[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 200, 34 + 50, painted), "the repaint pixel is readable");
        require(frame_pixel(directory, 500, 34 + 50, outside), "the control pixel is readable");
        require(is_magenta(painted), "a skipped layout still repaints the new background");
        require(!is_magenta(outside), "and the paint stops where the box does");
        std::system(("rm -rf " + directory).c_str());
    }

    // The layout key is a deny-list of paint-only properties, so a property the engine reads and
    // nobody classified still reflows. It used to be an allow-list, and it was missing `gap`,
    // `row-gap`, `column-gap`, `flex-direction`, `flex-grow`, `align-items`, `justify-content`,
    // `grid-template-columns` and `aspect-ratio` - all read by `layout.cpp`. Found by auditing
    // the list against what the engine reads, not by a test, which is why this one exists.
    {
        std::string directory = "aetheris-layout-key-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto gap = run_window(binary,
            "<html><head><title>Layout key gap</title><style>body{margin:0}"
            "#box{display:flex;width:400px;height:100px;background:#0000ff}"
            ".a{width:50px;height:100px;background:#ff00ff}"
            ".b{width:50px;height:100px;background:#00ff00}"
            "#box.on{gap:200px}</style></head>"
            "<body><div id='box'><div class='a'>a</div><div class='b'>b</div></div><script>"
            "setTimeout(function () { document.getElementById('box').setAttribute('class', 'on'); }, 5);"
            "</script></body></html>", "", 2, directory);
        require(gap.status == 0, "the gap run exits cleanly");
        require(summary_count(gap.output, ", relayouts: ") == 1, "a gap change relayouts once");
        require(summary_count(gap.output, ", layouts: ") == 2,
            "a class change that only moves \"gap\" runs the layout engine");
        int moved[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 250, 34 + 50, moved), "the second box pixel is readable");
        require(is_green(moved), "the box moved, so the frame is not stale");

        // The other direction: a colour change is paint-only and must still skip the pass.
        auto colour = run_window(binary,
            "<html><head><title>Layout key colour</title><style>body{margin:0}"
            "div{width:400px;height:100px;background:#0000ff;color:#ffffff}.on{color:#000000}"
            "</style></head><body><div id='a'>a</div><script>"
            "setTimeout(function () { document.getElementById('a').setAttribute('class', 'on'); }, 5);"
            "</script></body></html>", "", 2);
        require(colour.status == 0, "the colour run exits cleanly");
        require(summary_count(colour.output, ", layouts: ") == 1,
            "a class change that only changes colour does not run the layout engine");
        std::system(("rm -rf " + directory).c_str());
    }

    // Per-node layout: a subtree nothing restyled and that sits where it sat is copied from the
    // last pass instead of laid out. `layout nodes:` is the count that says so.
    {
        std::string rows;
        for (int index = 0; index < 200; ++index)
            rows += "<div class='r'" + std::string(index == 100 ? " id='mid'" : "") + ">row "
                + std::to_string(index) + "</div>";
        std::string const page = "<html><head><title>Per-node layout</title><style>body{margin:0}"
                                 "div.r{width:400px;height:3px;background:#00ff00}"
                                 "#mid{background:#0000ff}"
                                 "#first{width:400px;height:3px;background:#0000ff}"
                                 "#first.tall{height:60px}";

        // A text edit in row 100 that changes nothing else: the chain down to it is laid out,
        // the other 400 nodes are not.
        auto edit = run_window(binary,
            page + "</style></head><body>" + rows + "<script>"
                   "setTimeout(function () { document.getElementById('mid').textContent = 'ROW 100'; }, 5);"
                   "</script></body></html>",
            "", 2);
        require(edit.status == 0, "the per-node text edit run exits cleanly");
        require(summary_count(edit.output, ", relayouts: ") == 1, "the text edit relayouts once");
        int laid_out = summary_count(edit.output, ", layout nodes: ");
        require(laid_out > 0 && laid_out < 40,
            "a text edit in row 100 of 200 lays out the chain, not the page");

        // The control: growing the first row moves every row below it, so they cannot be reused
        // where they were and the pass must lay them out.
        std::string const directory = "aetheris-per-node-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto grow = run_window(binary,
            page + "</style></head><body><div id='first'>first</div>" + rows + "<script>"
                   "setTimeout(function () { document.getElementById('first').setAttribute('class', 'tall'); }, 5);"
                   "</script></body></html>",
            "", 2, directory);
        require(grow.status == 0, "the growing-row run exits cleanly");
        int moved = summary_count(grow.output, ", layout nodes: ");
        require(moved > 200,
            "rows that moved are laid out again, not reused where they used to be");
        // Content y 1 is inside the 60 px first row after the change; content y 70 is the first
        // row that follows it. Both were read off the frame before this assertion was written.
        int inside_first[3] = { 0, 0, 0 };
        int after_first[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 200, 34 + 1, inside_first), "the first-row pixel is readable");
        require(frame_pixel(directory, 200, 34 + 70, after_first), "the row-after pixel is readable");
        require(is_blue(inside_first), "the first row grew to 60 px");
        require(is_green(after_first), "and the rows below moved down with it");
        std::system(("rm -rf " + directory).c_str());
    }

    // A structural change invalidates the child sequence it changed, not the document. Upstream
    // records a tree delta for the departed or arrived sequence (StyleEngineInput.cpp); this is
    // the same set, and when the sheet has no positional selector it is the parent alone.
    {
        std::string rows;
        for (int index = 0; index < 200; ++index)
            rows += "<div class='r' id='r" + std::to_string(index) + "'>row " + std::to_string(index) + "</div>";
        std::string const page = "<html><head><title>Structural cost</title><style>body{margin:0}"
                                 "div.r{width:400px;height:3px;background:#00ff00}"
                                 "#r150{background:#0000ff}"
                                 "span{background:#ff00ff}";
        std::string const append = "<script>setTimeout(function () { var d = document.createElement('span');"
                                   "d.textContent = 'x'; document.getElementById('r150').appendChild(d); }, 5);"
                                   "</script></body></html>";

        // Appending inside one row of 200: the parent's subtree is what can see it.
        auto deep = run_window(binary, page + "</style></head><body>" + rows + append, "", 2);
        require(deep.status == 0, "the deep append run exits cleanly");
        int deep_resolves = summary_count(deep.output, ", resolves: ");
        require(deep_resolves > 0 && deep_resolves < 40,
            "appending inside one row of 200 re-resolves a handful of nodes, not the page");

        // Appending to body with no positional selector in the sheet: only the parent can restyle,
        // and the new child has no cache entry to reuse.
        std::string const append_to_body = "<script>setTimeout(function () { var d = document.createElement('div');"
                                           "d.textContent = 'new'; document.body.appendChild(d); }, 5);"
                                           "</script></body></html>";
        auto body_append = run_window(binary, page + "</style></head><body>" + rows + append_to_body, "", 2);
        require(body_append.status == 0, "the body append run exits cleanly");
        int body_resolves = summary_count(body_append.output, ", resolves: ");
        require(body_resolves > 0 && body_resolves < 40,
            "appending to body re-resolves the parent and the new node, not the 200 rows");

        // The control: one positional selector in the sheet and the same append must go coarse,
        // because :first-child can restyle any row when the sequence changes.
        auto positional = run_window(binary,
            page + "div.r:first-child{background:#ffff00}</style></head><body>" + rows + append_to_body, "", 2);
        require(positional.status == 0, "the positional run exits cleanly");
        require(summary_count(positional.output, ", resolves: ") > 200,
            "a positional selector makes a structural change re-resolve the child sequence");

        // The correctness control for the narrow path: `:empty` is the parent's own state, so the
        // parent is dirtied even when no sibling can see the change.
        std::string const directory = "aetheris-structural-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto empty_state = run_window(binary,
            "<html><head><title>Empty</title><style>body{margin:0}"
            "#box{width:400px;height:100px;background:#ff00ff}#box:empty{background:#0000ff}"
            "</style></head><body><div id='box'></div><script>"
            "setTimeout(function () { var d = document.createElement('span'); d.textContent = 'x';"
            "document.getElementById('box').appendChild(d); }, 5);</script></body></html>",
            "", 2, directory);
        require(empty_state.status == 0, "the :empty run exits cleanly");
        int box_pixel[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 200, 34 + 50, box_pixel), "the :empty pixel is readable");
        require(is_magenta(box_pixel),
            "appending a child restyles the parent, so :empty stopped matching");
        std::system(("rm -rf " + directory).c_str());
    }

    // A click is not a layout. dispatch_click_at() used to call relayout() unconditionally, and
    // relayout() with no mutation on record is a structural change to it: the style cache was
    // cleared and the whole page laid out. On a 405-node page that was 407 resolves and 405 nodes
    // laid out, for a handler that only logged.
    {
        std::string rows;
        for (int index = 0; index < 200; ++index)
            rows += "<div class='r'>row " + std::to_string(index) + "</div>";
        std::string const page = "<html><head><title>Click cost</title><style>body{margin:0}"
                                 "#a{width:400px;height:100px;background:#0000ff}"
                                 "#a.grow{width:200px}"
                                 "div.r{width:400px;height:3px;background:#00ff00}"
                                 "</style></head><body><div id='a'>A</div>" + rows + "<script>";

        auto static_page = run_window(binary,
            page + "document.getElementById('a').addEventListener('click', function () {"
                   "console.log('MARK clicked'); });</script></body></html>",
            "--click 200 80", 3);
        require(static_page.status == 0, "the static click run exits cleanly");
        require(static_page.output.find("MARK clicked") != std::string::npos,
            "the handler still runs when the click changes nothing");
        require(summary_count(static_page.output, ", layouts: ") == 1,
            "a click that changes nothing does not run the layout engine");

        // A click that writes an attribute no rule mentions rebuilds and then skips: the pass is
        // entered, and it lays nothing out.
        auto attribute = run_window(binary,
            page + "document.getElementById('a').addEventListener('click', function () {"
                   "document.getElementById('a').setAttribute('class', 'x'); });</script></body></html>",
            "--click 200 80", 3);
        require(attribute.status == 0, "the attribute click run exits cleanly");
        require(summary_count(attribute.output, ", layouts: ") == 1,
            "a click writing an unobserved attribute does not run the layout engine");
        require(summary_count(attribute.output, ", layout nodes: ") == 0,
            "and the counter says nothing was laid out, rather than repeating the last pass");

        // The control: a click that changes a width must reflow, and only the chain.
        auto layout_change = run_window(binary,
            page + "document.getElementById('a').addEventListener('click', function () {"
                   "document.getElementById('a').setAttribute('class', 'grow'); });</script></body></html>",
            "--click 200 80", 3);
        require(layout_change.status == 0, "the layout-click run exits cleanly");
        require(summary_count(layout_change.output, ", layouts: ") == 2,
            "a click that changes a width does run the layout engine");
        int clicked_nodes = summary_count(layout_change.output, ", layout nodes: ");
        require(clicked_nodes > 0 && clicked_nodes < 40,
            "and lays out the chain, not the page");
    }

    // Reftests, in upstream's shape: Tests/LibWeb/Ref pairs an input with a reference document
    // (`<link rel="match">`) and requires them to render identically. Here the input is a page
    // mutated at runtime into a state and the reference is that same state loaded fresh - the only
    // reference that cannot inherit my assumptions about what the incremental path produces.
    //
    // Every case runs over a 200-row page so that reuse is doing real work, and every case must
    // agree byte for byte below the chrome.
    {
        std::string const style = "<html><head><title>Reftest</title><style>body{margin:0}"
                                  "div.r{width:400px;height:3px;background:#00ff00}"
                                  "#mid{background:#0000ff}"
                                  "#mid.hit{width:200px}"
                                  "#mid.hot{background:#ff00ff}"
                                  "span.s{background:#ffff00}"
                                  "</style></head><body>";
        std::string const rows = [] {
            std::string out;
            for (int index = 0; index < 200; ++index) {
                if (index == 100)
                    out += "<div class='r' id='mid'>row 100</div>";
                else
                    out += "<div class='r'>row " + std::to_string(index) + "</div>";
            }
            return out;
        }();
        std::string const tail = "</body></html>";

        std::string const script_open = "<script>";
        std::string const script_close = "</script>";
        auto script_of = [&](std::string const& body) { return script_open + body + script_close; };

        struct Run {
            char const* what;
            std::string mutated;
            std::string baked;
        };
        std::vector<Run> const runs {
            { "a text write",
                style + rows + script_of("setTimeout(function () { document.getElementById('mid').textContent = 'ROW 100'; }, 5);") + tail,
                style + rows + tail },
            { "a width change by class",
                style + rows + script_of("setTimeout(function () { document.getElementById('mid').setAttribute('class', 'r hit'); }, 5);") + tail,
                style + [&] { std::string out; for (int index = 0; index < 200; ++index) out += index == 100 ? "<div class='r hit' id='mid'>row 100</div>" : "<div class='r'>row " + std::to_string(index) + "</div>"; return out; }() + tail },
            { "a paint-only class change",
                style + rows + script_of("setTimeout(function () { document.getElementById('mid').setAttribute('class', 'r hot'); }, 5);") + tail,
                style + [&] { std::string out; for (int index = 0; index < 200; ++index) out += index == 100 ? "<div class='r hot' id='mid'>row 100</div>" : "<div class='r'>row " + std::to_string(index) + "</div>"; return out; }() + tail },
            // setAttribute, not className: this engine has classList but no className setter, so
            // `s.className = 's'` would set a JS property and never reach the DOM - which is how
            // this case first failed, on the fixture rather than the engine.
            { "an appended child",
                style + rows + script_of("setTimeout(function () { var s = document.createElement('span'); s.setAttribute('class', 's'); s.textContent = 's'; document.getElementById('mid').appendChild(s); }, 5);") + tail,
                style + [&] { std::string out; for (int index = 0; index < 200; ++index) out += index == 100 ? "<div class='r' id='mid'>row 100<span class='s'>s</span></div>" : "<div class='r'>row " + std::to_string(index) + "</div>"; return out; }() + tail },
            { "an innerHTML replace",
                style + rows + script_of("setTimeout(function () { document.getElementById('mid').innerHTML = \"<span class='s'>s</span>\"; }, 5);") + tail,
                style + [&] { std::string out; for (int index = 0; index < 200; ++index) out += index == 100 ? "<div class='r' id='mid'><span class='s'>s</span></div>" : "<div class='r'>row " + std::to_string(index) + "</div>"; return out; }() + tail },
        };

        std::string const mutated_directory = "aetheris-reftest-mutated";
        std::string const reference_directory = "aetheris-reftest-reference";
        for (auto const& run : runs) {
            std::system(("rm -rf " + mutated_directory + " && mkdir -p " + mutated_directory).c_str());
            std::system(("rm -rf " + reference_directory + " && mkdir -p " + reference_directory).c_str());
            auto mutated = run_window(binary, run.mutated, "", 3, mutated_directory);
            std::string const input_message = std::string("the reftest input for ") + run.what + " exits cleanly";
            if (mutated.status != 0) {
                std::fprintf(stderr, "FAILED: %s (status %d)\n%s\n", input_message.c_str(), mutated.status,
                    mutated.output.substr(0, 400).c_str());
                std::exit(1);
            }
            auto reference = run_window(binary, run.baked, "", 2, reference_directory);
            std::string const reference_message = std::string("the reftest reference for ") + run.what + " exits cleanly";
            require(reference.status == 0, reference_message.c_str());
            require(frames_match_below_chrome(mutated_directory, reference_directory, 34), run.what);
        }
        std::system(("rm -rf " + mutated_directory + " && rm -rf " + reference_directory).c_str());
    }

    // The style cache survives an interaction-state change: only the elements whose
    // pseudo-class state moved are re-resolved, and a state that changes nothing but paint
    // does not run the layout engine. Upstream's ancestor_traversal_for_pseudo_class() is
    // the shape - `:focus-within` is the one pseudo-class that reaches ancestors.
    {
        // A hover that changes only a background: one relayout, still one layout.
        std::string directory = "aetheris-state-frames";
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto paint = run_window(binary,
            "<html><head><title>State paint</title><style>body{margin:0}"
            "#a{width:400px;height:100px;background:#0000ff}#a:hover{background:#ff00ff}"
            "</style></head><body><div id='a'>A</div></body></html>",
            "--move 200 80", 3, directory);
        require(paint.status == 0, "the hover-paint run exits cleanly");
        require(summary_count(paint.output, ", relayouts: ") == 1, "a hover relayouts once");
        require(summary_count(paint.output, ", layouts: ") == 1,
            "a hover that only changes paint does not run the layout engine");
        int hovered[3] = { 0, 0, 0 };
        int below[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 200, 34 + 50, hovered), "the hovered pixel is readable");
        require(frame_pixel(directory, 200, 34 + 150, below), "the control pixel is readable");
        require(is_magenta(hovered), "the hover restyled the element it moved onto");
        require(!is_magenta(below), "and nothing outside it");

        // The control: the same hover changing width must run the engine.
        auto width = run_window(binary,
            "<html><head><title>State width</title><style>body{margin:0}"
            "#a{width:400px;height:100px;background:#0000ff}#a:hover{width:200px}"
            "</style></head><body><div id='a'>A</div></body></html>",
            "--move 200 80", 3);
        require(width.status == 0, "the hover-width run exits cleanly");
        require(summary_count(width.output, ", relayouts: ") == 1, "a width hover relayouts once");
        require(summary_count(width.output, ", layouts: ") == 2,
            "a hover that changes width does run the layout engine");

        // The cache is what makes it cheap: a hover on one row of a 200-row page must
        // re-resolve a handful of nodes, not the page. Before this it cleared the whole
        // cache and resolved all 406.
        std::string rows;
        for (int index = 0; index < 200; ++index)
            rows += "<div class='r'>row " + std::to_string(index) + "</div>";
        auto many = run_window(binary,
            "<html><head><title>State many</title><style>body{margin:0}"
            "#target{width:400px;height:20px;background:#0000ff}"
            "div.r{width:400px;height:3px;background:#00ff00}"
            "#target:hover{background:#ff00ff}</style></head>"
            "<body><div id='target'>t</div>" + rows + "</body></html>",
            "--move 200 44", 3);
        require(many.status == 0, "the 200-row hover run exits cleanly");
        require(summary_count(many.output, ", layouts: ") == 1,
            "a hover in a 200-row page still costs no layout");
        int resolved = summary_count(many.output, ", resolves: ");
        require(resolved > 0 && resolved < 40,
            "a hover in a 200-row page re-resolves a handful of nodes, not the page");
        // The page is 406 nodes; the key comparison must be over the 5 that restyled, since
        // only a node whose style was re-resolved can have a different key.
        require(summary_count(many.output, ", key checks: ") == resolved,
            "the layout key comparison covers the nodes that restyled, not the page");
        require(many.output.find("relayouts: 0") == std::string::npos,
            "and it still relayouts, so the hover is not dropped");

        // A descendant selector on a hovered ancestor: the subtree is dirty too.
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto descendant = run_window(binary,
            "<html><head><title>State descendant</title><style>body{margin:0}"
            "#card{width:400px;height:100px;background:#0000ff}"
            "#card .title{width:100px;height:40px;background:#00ff00}"
            "#card:hover .title{background:#ff00ff}</style></head>"
            "<body><div id='card'><span class='title'>t</span></div></body></html>",
            "--move 200 80", 3, directory);
        require(descendant.status == 0, "the hover-descendant run exits cleanly");
        require(summary_count(descendant.output, ", layouts: ") == 1,
            "a descendant selector on a hovered ancestor costs no layout");
        int title_pixel[3] = { 0, 0, 0 };
        int card_pixel[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 50, 34 + 10, title_pixel), "the descendant pixel is readable");
        require(frame_pixel(directory, 200, 34 + 80, card_pixel), "the card pixel is readable");
        require(is_magenta(title_pixel), "a descendant of the hovered element restyled");
        require(is_blue(card_pixel), "and the hovered element kept its own colour");

        // `:focus-within` is the one pseudo-class that propagates to ancestors, so a focus
        // move has to restyle the chain above - and a descendant of that ancestor that is a
        // sibling of the focused element, which is the case a chain-only dirty set misses.
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto within = run_window(binary,
            "<html><head><title>State focus-within</title><style>body{margin:0}"
            "#card{width:400px;height:200px;background:#0000ff}"
            "#card .title{width:400px;height:40px;background:#00ff00}"
            "#card:focus-within{background:#ffff00}"
            "#card:focus-within .title{background:#ff00ff}"
            "input{width:200px;height:40px}</style></head>"
            "<body><div id='card'><span class='title'>t</span><input id='field'></div></body></html>",
            "--focus-key 200 140", 3, directory);
        require(within.status == 0, "the focus-within run exits cleanly");
        require(summary_count(within.output, ", relayouts: ") == 1, "a focus change relayouts once");
        require(summary_count(within.output, ", layouts: ") == 1,
            "a focus change that only changes paint does not run the layout engine");
        int ancestor_pixel[3] = { 0, 0, 0 };
        int sibling_pixel[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 200, 34 + 150, ancestor_pixel), "the ancestor pixel is readable");
        require(frame_pixel(directory, 200, 34 + 10, sibling_pixel), "the sibling pixel is readable");
        require(ancestor_pixel[0] > 200 && ancestor_pixel[1] > 200 && ancestor_pixel[2] < 100,
            ":focus-within restyled the ancestor, so the ancestor chain is in the dirty set");
        require(is_magenta(sibling_pixel),
            "a descendant of the :focus-within ancestor restyled, so its subtree is too");

        // An ancestor's subtree is only dirtied when a rule can put `:focus-within` on *that*
        // element. Asked sheet-wide, one `.card:focus-within .title` rule made every focus move
        // in the document coarse; asked per element, it only affects the card.
        //
        // The title's base rule is `.title`, one class, so the `:focus-within` rule below wins
        // on specificity. With `#card .title` as the base it would win instead, the rule would
        // never apply, and this would have been a fixture that tested nothing.
        {
            std::string rows;
            for (int index = 0; index < 200; ++index)
                rows += "<div class='r'>row " + std::to_string(index) + "</div>";
            std::string const page = "<html><head><title>State focus rows</title><style>body{margin:0}"
                                     "#card{width:400px;height:100px;background:#0000ff}"
                                     ".title{width:100px;height:20px;background:#00ff00}"
                                     "div.r{width:400px;height:3px;background:#00ff00}"
                                     "input{display:block;width:100px;height:30px;margin:0}";
            std::string const body = "</style></head><body><div id='card'><span class='title'>t</span>"
                                     "<input id='inside'></div><input id='outside'>" + rows
                + "</body></html>";
            std::string const directory = "aetheris-focus-frames";

            // A rule of that shape exists, but the focus is outside the card, so the card's
            // subtree is not dirty - the sheet-wide answer would have dirtied the page.
            std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
            auto outside = run_window(binary,
                page + "#card:focus-within .title{background:#ff00ff}" + body,
                "--focus-key 50 149", 3, directory);
            require(outside.status == 0, "the focus-outside run exits cleanly");
            require(summary_count(outside.output, ", layouts: ") == 1,
                "a focus move on a 200-row page costs no layout");
            int outside_resolves = summary_count(outside.output, ", resolves: ");
            require(outside_resolves > 0 && outside_resolves < 40,
                "a rule on another card's :focus-within does not make this focus move coarse");
            int untouched[3] = { 0, 0, 0 };
            require(frame_pixel(directory, 50, 34 + 10, untouched), "the title pixel is readable");
            require(is_green(untouched), "a card that is not focus-within keeps its title");

            // The same rule, focus inside the card: cheap, and the title must restyle.
            std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
            auto inside = run_window(binary,
                page + "#card:focus-within .title{background:#ff00ff}" + body,
                "--focus-key 50 74", 3, directory);
            require(inside.status == 0, "the focus-inside run exits cleanly");
            int inside_resolves = summary_count(inside.output, ", resolves: ");
            require(inside_resolves > 0 && inside_resolves < 40,
                "the card the focus moved into is re-resolved, and the page is not");
            int restyled[3] = { 0, 0, 0 };
            require(frame_pixel(directory, 50, 34 + 10, restyled), "the restyled title pixel is readable");
            require(is_magenta(restyled), "the descendant of the :focus-within card restyled");

            // The control: a compound that matches a wide ancestor must go coarse, or the two
            // assertions above would hold whether or not the sheet was ever consulted.
            auto coarse = run_window(binary,
                page + "body:focus-within .title{background:#ff00ff}" + body,
                "--focus-key 50 74", 3);
            require(coarse.status == 0, "the coarse focus run exits cleanly");
            int coarse_resolves = summary_count(coarse.output, ", resolves: ");
            require(coarse_resolves > 200,
                "a :focus-within rule on body re-resolves the page");
            std::system(("rm -rf " + directory).c_str());
        }

        // `:active` is a separate branch of the same switch and must take the same path.
        std::system(("rm -rf " + directory + " && mkdir -p " + directory).c_str());
        auto pressed = run_window(binary,
            "<html><head><title>State active</title><style>body{margin:0}"
            "#a{width:400px;height:100px;background:#0000ff}#a:active{background:#ff00ff}"
            "</style></head><body><div id='a'>A</div></body></html>",
            "--press 200 80", 3, directory);
        require(pressed.status == 0, "the :active run exits cleanly");
        require(summary_count(pressed.output, ", relayouts: ") == 1, ":active relayouts once");
        require(summary_count(pressed.output, ", layouts: ") == 1,
            "an :active that only changes paint does not run the layout engine");
        int pressed_pixel[3] = { 0, 0, 0 };
        require(frame_pixel(directory, 200, 34 + 50, pressed_pixel), "the :active pixel is readable");
        require(is_magenta(pressed_pixel), ":active restyled the pressed element");

        // The control: `:focus-within` changing width must run the engine.
        auto within_width = run_window(binary,
            "<html><head><title>State focus-within width</title><style>body{margin:0}"
            "#card{width:400px;height:200px;background:#0000ff}"
            "#card:focus-within{width:200px}"
            "input{width:200px;height:40px}</style></head>"
            "<body><div id='card'><input id='field'></div></body></html>",
            "--focus-key 200 100", 3);
        require(within_width.status == 0, "the focus-within width run exits cleanly");
        require(summary_count(within_width.output, ", layouts: ") == 2,
            "a focus change that changes width does run the layout engine");
        std::system(("rm -rf " + directory).c_str());
    }

    // The frame dumps above total ~58 MB and this sandbox's /tmp is a 64 MB tmpfs, so a
    // second run in the same sandbox used to start with /tmp full and fail elsewhere for
    // a reason that had nothing to do with the engine - AetherisDomHost's "render exits 0"
    // was a full disk, not a bad render. Clear them on the way out.
    std::system("rm -rf /tmp/aetheris-*-frames");

    std::printf("window scripts: ran, clicked, keyed, hovered, pressed, ticked, laid out, exited cleanly\n");
    return 0;
}

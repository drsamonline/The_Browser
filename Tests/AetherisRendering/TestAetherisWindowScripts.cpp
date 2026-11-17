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

    std::printf("window scripts: ran, clicked, keyed, hovered, pressed, ticked, laid out, exited cleanly\n");
    return 0;
}

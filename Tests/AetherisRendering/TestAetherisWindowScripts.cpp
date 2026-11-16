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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

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
    std::string const& extra_arguments, int frames = 2)
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
    std::string command = "'" + binary + "' --window " + url + " " + std::to_string(frames)
        + " " + extra_arguments + " 2>&1";
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

    std::printf("window scripts: ran, clicked, keyed, hovered, ticked, laid out and exited cleanly\n");
    return 0;
}

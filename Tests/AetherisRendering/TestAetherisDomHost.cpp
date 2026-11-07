// Drives the real `aetheris` binary against a loopback page and checks the
// JavaScript DOM host end to end: innerHTML, style, classList, selectors,
// textContent and event bubbling.
//
// This exists because the host wiring lives in main.cpp rather than a library,
// so the unit tests cannot reach it. A use-after-free in the innerHTML path
// shipped once with every console line still correct; asserting on stderr is
// what catches that class of bug.

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

static bool contains(std::string const& haystack, std::string const& needle)
{
    return haystack.find(needle) != std::string::npos;
}

#if defined(_WIN32)

// The harness below uses POSIX sockets and popen; the host path itself is
// cross-platform, but this test is skipped on Windows.
int main() { return 0; }

#else

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static const char* kPage =
    "<html><body>"
    "<div id='box'><span id='old'>old</span></div>"
    "<div id='outer'><span id='inner'>tap</span></div>"
    "<script>"
    "var box = document.getElementById('box');"
    "console.log('MARK before', box.innerHTML);"
    "box.innerHTML = '<p class=\"new\">fresh</p>';"
    "console.log('MARK after', box.innerHTML);"
    "console.log('MARK children', box.children.length);"
    "console.log('MARK query', document.querySelector('.new').textContent);"
    "console.log('MARK class0', box.classList.contains('x'));"
    "box.classList.add('x');"
    "console.log('MARK class1', box.classList.contains('x'));"
    "box.style = 'color: red';"
    "console.log('MARK style', box.getAttribute('style'));"
    "box.textContent = 'replaced';"
    "console.log('MARK text', box.textContent);"
    "document.getElementById('inner').onclick = function () { console.log('MARK inner'); };"
    "document.getElementById('outer').onclick = function () { console.log('MARK outer'); };"
    "</script></body></html>";

static uint16_t start_server(std::thread& server_thread)
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

    server_thread = std::thread([listener]() {
        for (;;) {
            // Wait for a connection, but stop once the client goes quiet so the
            // thread can be joined.
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(listener, &readable);
            struct timeval timeout {};
            timeout.tv_sec = 2;
            if (::select(listener + 1, &readable, nullptr, nullptr, &timeout) <= 0)
                break;
            int connection = ::accept(listener, nullptr, nullptr);
            if (connection < 0)
                break;
            char buffer[4096];
            ::recv(connection, buffer, sizeof(buffer), 0);
            std::string body = kPage;
            std::string response = "HTTP/1.1 200 OK\r\n";
            response += "Content-Type: text/html\r\n";
            response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
            response += "Connection: close\r\n\r\n";
            response += body;
            ::send(connection, response.data(), response.size(), 0);
            ::close(connection);
        }
        ::close(listener);
    });
    return port;
}

// Runs the browser binary with the given arguments, capturing stdout+stderr.
static int run_browser(std::string const& binary, std::vector<std::string> const& arguments, std::string& captured)
{
    std::string command = "\"" + binary + "\"";
    for (auto const& argument : arguments)
        command += " \"" + argument + "\"";
    command += " 2>&1";

    FILE* pipe = ::popen(command.c_str(), "r");
    if (!pipe)
        return -1;
    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe))
        captured += buffer;
    int status = ::pclose(pipe);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void require_clean_run(std::string const& captured)
{
    require(!contains(captured, "malloc"), "no allocator corruption");
    require(!contains(captured, "free()"), "no free() corruption");
    require(!contains(captured, "Segmentation fault"), "no segfault");
    require(!contains(captured, "corrupted"), "no heap corruption message");
    require(!contains(captured, "Aborted"), "no abort");
}

int main(int argc, char* argv[])
{
    require(argc >= 2, "browser binary path argument");
    std::string binary = argv[1];

    std::thread server;
    auto port = start_server(server);
    auto url = "http://127.0.0.1:" + std::to_string(port) + "/";
    auto output = "/tmp/aetheris-dom-host-test.ppm";

    // Pass 1: the DOM property surface.
    std::string captured;
    int status = run_browser(binary, { "--render", url, output }, captured);
    require(status == 0, "render exits 0");
    require_clean_run(captured);

    require(contains(captured, "MARK before <span id=\"old\">old</span>"), "innerHTML getter serialises the subtree");
    require(contains(captured, "MARK after <p class=\"new\">fresh</p>"), "innerHTML setter replaces the subtree");
    require(contains(captured, "MARK children 1"), "children reflects the new subtree");
    require(contains(captured, "MARK query fresh"), "querySelector finds a freshly grafted element");
    require(contains(captured, "MARK class0 false"), "classList.contains is false before add");
    require(contains(captured, "MARK class1 true"), "classList.add updates the class attribute");
    require(contains(captured, "MARK style color: red"), "style assignment writes the style attribute");
    require(contains(captured, "MARK text replaced"), "textContent accessor reads back the new text");

    // Pass 2: an element event bubbles to its ancestor.
    std::string captured_dispatch;
    status = run_browser(binary, { "--render", url, output, "--dispatch", "inner", "click" }, captured_dispatch);
    require(status == 0, "dispatch render exits 0");
    require_clean_run(captured_dispatch);
    require(contains(captured_dispatch, "MARK inner"), "the target handler runs");
    require(contains(captured_dispatch, "MARK outer"), "the event bubbles to the ancestor");

    server.join();
    return 0;
}

#endif

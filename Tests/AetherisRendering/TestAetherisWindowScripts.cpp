// A window is a browser, so a page's scripts must run in it. This drives the
// real binary in --window mode against a local fixture and checks two things:
// the script's console output appears, and the process exits cleanly. The exit
// code matters as much as the output: destroying the interpreter while the DOM
// host still held JS handles used to abort at teardown.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
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

static const char* kPage =
    "<html><head><title>Scripted</title></head><body>"
    "<div id='host'>before</div>"
    "<script>console.log('MARK window script ran');"
    "document.getElementById('host').textContent = 'built by script';</script>"
    "</body></html>";

int main(int argc, char* argv[])
{
    require(argc >= 2, "browser binary path argument");
    std::string binary = argv[1];

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

    // Serves until the child closes: no idle timeout, so a slow machine cannot
    // lose the server between requests.
    std::thread server([listener]() {
        for (;;) {
            int connection = ::accept(listener, nullptr, nullptr);
            if (connection < 0)
                break;
            char buffer[4096];
            ::recv(connection, buffer, sizeof(buffer), 0);
            std::string body = kPage;
            std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n";
            response += "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
            response += body;
            ::send(connection, response.data(), response.size(), 0);
            ::close(connection);
        }
        ::close(listener);
    });

    std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
    std::string command = "'" + binary + "' --window " + url + " 2 2>&1";
    FILE* pipe = ::popen(command.c_str(), "r");
    require(pipe != nullptr, "popen");
    std::string captured;
    char line[512];
    while (std::fgets(line, sizeof(line), pipe))
        captured += line;
    int status = ::pclose(pipe);

    // Stop the server: a connection to a closed listener ends the accept loop.
    ::shutdown(listener, SHUT_RDWR);
    ::close(listener);
    server.join();

    require(status == 0, "the window run exits cleanly");
    require(captured.find("MARK window script ran") != std::string::npos,
        "scripts run in the window loop");
    require(captured.find("Window 1024x768") != std::string::npos, "the window loop completed");
    std::printf("window scripts: ran and exited cleanly\n");
    return 0;
}

#include "aetheris/network/http_client.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

using namespace aetheris::network;

static void require(bool condition, char const* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

#if AETHERIS_PLATFORM_WINDOWS

// The loopback server below uses POSIX sockets; the HTTP client itself is
// cross-platform, but this particular test is skipped on Windows.
int main() { return 0; }

#else

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

static constexpr char const* kBody = "<html><body><h1>Hello</h1></body></html>";

// Starts a one-shot HTTP/1.1 server on the loopback interface and returns the
// port it is listening on. The server thread accepts a single connection,
// replies with a fixed 200 response and exits.
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
    require(::listen(listener, 1) == 0, "listen");

    socklen_t length = sizeof(address);
    require(::getsockname(listener, reinterpret_cast<struct sockaddr*>(&address), &length) == 0, "getsockname");
    uint16_t port = ntohs(address.sin_port);

    server_thread = std::thread([listener]() {
        int connection = ::accept(listener, nullptr, nullptr);
        if (connection >= 0) {
            char buffer[1024];
            ::recv(connection, buffer, sizeof(buffer), 0);
            std::string response = "HTTP/1.1 200 OK\r\n";
            response += "Content-Type: text/html\r\n";
            response += "Content-Length: ";
            response += std::to_string(std::strlen(kBody));
            response += "\r\nConnection: close\r\n\r\n";
            response += kBody;
            ::send(connection, response.data(), response.size(), 0);
            ::close(connection);
        }
        ::close(listener);
    });
    return port;
}

static void check_ok(HttpResponseData const& response)
{
    require(response.error.empty(), "no transport error");
    require(response.status == 200, "status 200");
    require(response.header("Content-Type") == "text/html", "content type");
    require(response.body == kBody, "body round-trips");
}

int main()
{
    require(Socket::initialize() == SocketError::None, "socket init");

    // IP-literal path.
    {
        std::thread server;
        auto port = start_server(server);
        check_ok(HttpClient::get("http://127.0.0.1:" + std::to_string(port) + "/"));
        server.join();
    }

    // Hostname path (exercises getaddrinfo).
    {
        std::thread server;
        auto port = start_server(server);
        check_ok(HttpClient::get("http://localhost:" + std::to_string(port) + "/index.html"));
        server.join();
    }

    // A malformed URL is reported, not crashed on.
    {
        auto response = HttpClient::get("not-a-url");
        require(!response.error.empty(), "invalid url reported");
    }

    Socket::cleanup();
    return 0;
}

#endif

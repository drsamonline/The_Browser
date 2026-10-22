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

// A two-request server: the first response redirects and sets a cookie; the
// second records the Cookie header it received and returns 200.
static uint16_t start_redirect_server(std::thread& server_thread, std::string& captured_cookie)
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
    require(::listen(listener, 2) == 0, "listen");

    socklen_t length = sizeof(address);
    require(::getsockname(listener, reinterpret_cast<struct sockaddr*>(&address), &length) == 0, "getsockname");
    uint16_t port = ntohs(address.sin_port);

    server_thread = std::thread([listener, &captured_cookie]() {
        for (int request_index = 0; request_index < 2; ++request_index) {
            int connection = ::accept(listener, nullptr, nullptr);
            if (connection < 0)
                break;
            std::string request;
            char buffer[2048];
            while (request.find("\r\n\r\n") == std::string::npos) {
                auto bytes = ::recv(connection, buffer, sizeof(buffer), 0);
                if (bytes <= 0)
                    break;
                request.append(buffer, static_cast<size_t>(bytes));
            }
            if (request_index == 0) {
                std::string response = "HTTP/1.1 302 Found\r\n";
                response += "Location: /final\r\n";
                response += "Set-Cookie: session=abc; Path=/\r\n";
                response += "Content-Length: 0\r\nConnection: close\r\n\r\n";
                ::send(connection, response.data(), response.size(), 0);
            } else {
                auto cookie_pos = request.find("Cookie:");
                if (cookie_pos != std::string::npos) {
                    auto line_end = request.find("\r\n", cookie_pos);
                    captured_cookie = request.substr(cookie_pos + 7, line_end - cookie_pos - 7);
                    auto begin = captured_cookie.find_first_not_of(" ");
                    captured_cookie = captured_cookie.substr(begin);
                }
                std::string body = "final";
                std::string response = "HTTP/1.1 200 OK\r\n";
                response += "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
                response += body;
                ::send(connection, response.data(), response.size(), 0);
            }
            ::close(connection);
        }
        ::close(listener);
    });
    return port;
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

    // Redirects are followed and cookies carry across to the target.
    {
        HttpClient::clear_cookies();
        std::thread server;
        std::string captured_cookie;
        auto port = start_redirect_server(server, captured_cookie);
        auto response = HttpClient::get("http://127.0.0.1:" + std::to_string(port) + "/start");
        server.join();
        require(response.error.empty(), "redirect: no transport error");
        require(response.status == 200, "redirect followed to 200");
        require(response.body == "final", "redirect target body");
        require(captured_cookie == "session=abc", "cookie carried on the redirect request");
    }

    Socket::cleanup();
    return 0;
}

#endif

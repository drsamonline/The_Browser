// Row 0.3: the HTTP client had no proxy support, so a browser behind a corporate proxy could not
// reach anything - `--fetch https://example.com` said "could not connect" while `curl` succeeded.
//
// The proxy itself is a loopback server here, so this test proves the protocol without needing the
// internet: a real proxy records what the client sent, and the assertions are about that recording.
#include "aetheris/network/http_client.hpp"
#include "aetheris/network/proxy.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace aetheris::network;

[[maybe_unused]] static void require(bool condition, char const* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

#if AETHERIS_PLATFORM_WINDOWS

// The fake proxy below uses POSIX sockets; the client itself is cross-platform.
int main() { return 0; }

#else

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

static constexpr char const* kBody = "<html><body>through the proxy</body></html>";

// A one-shot proxy on the loopback interface. It records the first request line it sees and the
// headers that follow, then answers with either a 200 body or a CONNECT acceptance.
static uint16_t start_proxy(std::thread& proxy_thread, std::string& captured_request,
    bool answer_connect)
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
    uint16_t const port = ntohs(address.sin_port);

    proxy_thread = std::thread([listener, &captured_request, answer_connect]() {
        int connection = ::accept(listener, nullptr, nullptr);
        if (connection >= 0) {
            // Read the request head: everything up to the blank line.
            std::string raw;
            char buffer[1024];
            while (raw.find("\r\n\r\n") == std::string::npos) {
                auto const received = ::recv(connection, buffer, sizeof(buffer), 0);
                if (received <= 0)
                    break;
                raw.append(buffer, static_cast<std::size_t>(received));
            }
            captured_request = raw;

            if (answer_connect) {
                // A tunnel is accepted, and then this fake has nothing to say: the client will try
                // to start TLS against a plain socket, which is the expected end of this path.
                static constexpr char const* accepted = "HTTP/1.1 200 Connection established\r\n\r\n";
                ::send(connection, accepted, std::strlen(accepted), 0);
            } else {
                std::string response = "HTTP/1.1 200 OK\r\n";
                response += "Content-Type: text/html\r\n";
                response += "Content-Length: ";
                response += std::to_string(std::strlen(kBody));
                response += "\r\nConnection: close\r\n\r\n";
                response += kBody;
                ::send(connection, response.data(), response.size(), 0);
            }
            ::close(connection);
        }
        ::close(listener);
    });
    return port;
}

static void check_parse()
{
    // A full variable, as a corporate network writes it.
    auto const full = ProxyEndpoint::parse("http://alice:s%3Acret@proxy.corp.test:3128");
    require(full.has_value(), "a full proxy URL parses");
    require(full->type == ProxyEndpoint::Type::Http, "http scheme");
    require(full->host == "proxy.corp.test", "host");
    require(full->port == 3128, "port");
    require(full->describe() == "http://proxy.corp.test:3128", "describe() omits the credentials");
    require(full->authorization_header() == "Basic YWxpY2U6czpjcmV0",
        "Proxy-Authorization is Basic base64(user:pass), with the userinfo percent-decoded");

    // No scheme, no port: libcurl's default for an http proxy.
    auto const bare = ProxyEndpoint::parse("proxy.internal");
    require(bare.has_value() && bare->port == 1080, "a bare host takes the default proxy port");
    require(bare->authorization_header().empty(), "no userinfo means no Authorization header");

    // An https proxy keeps 443.
    auto const secure = ProxyEndpoint::parse("https://proxy.internal");
    require(secure.has_value() && secure->type == ProxyEndpoint::Type::Https && secure->port == 443,
        "an https proxy defaults to 443");

    // Bracketed IPv6.
    auto const v6 = ProxyEndpoint::parse("http://[::1]:8080");
    require(v6.has_value() && v6->host == "::1" && v6->port == 8080, "a bracketed IPv6 host parses");

    // Things this engine cannot use are refused rather than treated as "no proxy".
    require(!ProxyEndpoint::parse("socks5://proxy.internal:1080").has_value(), "socks5 is refused");
    require(!ProxyEndpoint::parse("").has_value(), "an empty variable is not a proxy");
    require(!ProxyEndpoint::parse("http://proxy.internal:0").has_value(), "port 0 is refused");
    require(!ProxyEndpoint::parse("http://proxy.internal:99999").has_value(), "a port above 65535 is refused");
    require(!ProxyEndpoint::parse("http://").has_value(), "a scheme with no host is refused");
}

static void check_configuration()
{
    // Precedence, as upstream implements it: lowercase first, HTTPS_PROXY as the fallback, and
    // HTTP_PROXY deliberately unread.
    auto const lookup = [](std::vector<std::pair<std::string, std::string>> const& values) {
        return [values](std::string_view name) -> std::optional<std::string> {
            for (auto const& entry : values)
                if (entry.first == name)
                    return entry.second;
            return {};
        };
    };

    auto const both = ProxyConfiguration::from_lookup(lookup({ { "http_proxy", "http://p1:8080" },
        { "https_proxy", "http://p2:8080" }, { "HTTP_PROXY", "http://ignored:1" } }));
    require(both.proxy_for("http", "example.test")->host == "p1", "http_proxy is used for http");
    require(both.proxy_for("https", "example.test")->host == "p2", "https_proxy is used for https");

    // HTTP_PROXY alone must not be picked up: a CGI environment can set it from a request header.
    auto const cgi = ProxyConfiguration::from_lookup(lookup({ { "HTTP_PROXY", "http://ignored:1" } }));
    require(!cgi.proxy_for("http", "example.test").has_value(), "HTTP_PROXY is ignored");

    // The uppercase https form is the fallback, which is what this sandbox sets.
    auto const upper = ProxyConfiguration::from_lookup(lookup({ { "HTTPS_PROXY", "http://p3:8080" } }));
    require(upper.proxy_for("https", "example.test")->host == "p3", "HTTPS_PROXY is a fallback for https");

    // all_proxy covers both, but an explicit variable wins over it.
    auto const all = ProxyConfiguration::from_lookup(lookup({ { "all_proxy", "http://p4:8080" } }));
    require(all.proxy_for("http", "example.test")->host == "p4", "all_proxy covers http");
    require(all.proxy_for("https", "example.test")->host == "p4", "all_proxy covers https");
    auto const overridden = ProxyConfiguration::from_lookup(lookup({ { "all_proxy", "http://p4:8080" },
        { "https_proxy", "http://p5:8080" } }));
    require(overridden.proxy_for("https", "example.test")->host == "p5", "https_proxy wins over all_proxy");

    // no_proxy, in the forms a network actually writes.
    auto const excluded = ProxyConfiguration::from_lookup(lookup({ { "http_proxy", "http://p6:8080" },
        { "no_proxy", "localhost,127.0.0.1,.corp.test, exact.test:8080" } }));
    require(!excluded.proxy_for("http", "localhost").has_value(), "localhost is excluded");
    require(!excluded.proxy_for("http", "127.0.0.1").has_value(), "a loopback address is excluded");
    require(!excluded.proxy_for("http", "api.corp.test").has_value(), "a .domain entry excludes subdomains");
    require(!excluded.proxy_for("http", "corp.test").has_value(), "and the domain itself");
    require(!excluded.proxy_for("http", "exact.test").has_value(), "a port in the entry is ignored");
    require(excluded.proxy_for("http", "other.test").has_value(), "an unrelated host is still proxied");
    require(!excluded.proxy_for("http", "notcorp.test").has_value() == false, "a suffix match needs the dot");

    auto const all_excluded = ProxyConfiguration::from_lookup(lookup({ { "http_proxy", "http://p7:8080" },
        { "no_proxy", "*" } }));
    require(!all_excluded.proxy_for("http", "anything.test").has_value(), "* excludes everything");
}

int main()
{
    check_parse();
    check_configuration();

    // End to end: a plain-http request goes to the proxy, addressed absolutely, and carries the
    // Proxy-Authorization the variable asked for.
    {
        std::string captured;
        std::thread proxy_thread;
        auto const port = start_proxy(proxy_thread, captured, false);

        auto const variable = "http://alice:secret@127.0.0.1:" + std::to_string(port);
        ::setenv("http_proxy", variable.c_str(), 1);
        ::unsetenv("no_proxy");
        ::unsetenv("NO_PROXY");
        HttpClient::clear_connections();

        auto const response = HttpClient::get("http://example.test/page");
        proxy_thread.join();

        require(response.error.empty(), "the request completed through the proxy");
        require(response.body == kBody, "the body came back from the proxy");

        // What the proxy was actually told.
        require(captured.rfind("GET http://example.test/page HTTP/1.1\r\n", 0) == 0,
            "the request target is the absolute URL, not the path");
        require(captured.find("Proxy-Authorization: Basic YWxpY2U6c2VjcmV0\r\n") != std::string::npos,
            "the proxy credentials were sent");
        require(captured.find("Host: example.test\r\n") != std::string::npos, "the origin host is still sent");

        ::unsetenv("http_proxy");
    }

    // End to end: an https request opens a tunnel with CONNECT, naming the origin, and puts the
    // credentials on that CONNECT rather than on the request inside the tunnel.
    {
        std::string captured;
        std::thread proxy_thread;
        auto const port = start_proxy(proxy_thread, captured, true);

        auto const variable = "http://alice:secret@127.0.0.1:" + std::to_string(port);
        ::setenv("https_proxy", variable.c_str(), 1);
        ::unsetenv("http_proxy");
        ::unsetenv("no_proxy");
        ::unsetenv("NO_PROXY");
        HttpClient::clear_connections();

        auto const response = HttpClient::get("https://example.test/secure");
        proxy_thread.join();

        require(captured.rfind("CONNECT example.test:443 HTTP/1.1\r\n", 0) == 0,
            "the tunnel is asked for with CONNECT, naming the origin's host and port");
        require(captured.find("Proxy-Authorization: Basic YWxpY2U6c2VjcmV0\r\n") != std::string::npos,
            "the credentials go on the CONNECT");
        // The fake proxy is not a TLS server, so the handshake inside the tunnel must fail - and it
        // must fail as a TLS error, not as "could not connect", which is what row 0.3 was about.
        require(!response.error.empty(), "the request cannot succeed against a plain socket");
        require(response.error.find("Could not connect") == std::string::npos,
            "the tunnel opened, so the failure is not a connection failure");

        ::unsetenv("https_proxy");
    }

    return 0;
}

#endif

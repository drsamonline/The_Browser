// Hermetic TLS test: an in-process OpenSSL server presents an embedded
// self-signed localhost certificate and the TlsStream client performs a real
// handshake, certificate/hostname verification and an HTTP exchange.

#include "aetheris/network/tls.hpp"
#include "tls_cert_data.h"

#include <openssl/ssl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace aetheris::network;

static void require(bool condition, char const* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

static std::string write_temp(char const* name, char const* content)
{
    std::string path = std::string("/tmp/aetheris_tls_") + name;
    std::ofstream stream(path);
    stream << content;
    return path;
}

int main()
{
    require(TlsStream::available(), "TLS support is built in");

    std::string cert_path = write_temp("cert.pem", kTlsCertPem);
    std::string key_path = write_temp("key.pem", kTlsKeyPem);

    SSL_CTX* server_ctx = SSL_CTX_new(TLS_server_method());
    require(server_ctx != nullptr, "server context");
    require(SSL_CTX_use_certificate_file(server_ctx, cert_path.c_str(), SSL_FILETYPE_PEM) == 1, "load server certificate");
    require(SSL_CTX_use_PrivateKey_file(server_ctx, key_path.c_str(), SSL_FILETYPE_PEM) == 1, "load server key");

    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    require(listen_fd >= 0, "listen socket");
    int reuse = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    require(::bind(listen_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "bind");
    require(::listen(listen_fd, 4) == 0, "listen");
    socklen_t length = sizeof(address);
    require(::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&address), &length) == 0, "getsockname");
    unsigned short port = ntohs(address.sin_port);

    // Serve two connections: one trusted (verifies), one untrusted (must fail).
    std::thread server([&] {
        for (int i = 0; i < 2; ++i) {
            int fd = ::accept(listen_fd, nullptr, nullptr);
            if (fd < 0)
                return;
            SSL* ssl = SSL_new(server_ctx);
            SSL_set_fd(ssl, fd);
            if (SSL_accept(ssl) == 1) {
                char buffer[1024];
                int read = SSL_read(ssl, buffer, sizeof(buffer));
                if (read > 0) {
                    static const char response[] =
                        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello";
                    SSL_write(ssl, response, static_cast<int>(std::strlen(response)));
                }
            }
            SSL_shutdown(ssl);
            SSL_free(ssl);
            ::close(fd);
        }
    });

    // Trusted: the embedded certificate is supplied as the CA bundle.
    TlsStream trusted;
    require(trusted.connect("127.0.0.1", port, 5000, cert_path), trusted.error().c_str());
    std::string request = "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    require(trusted.write(request), "TLS write");

    std::string response;
    char buffer[512];
    std::size_t received = 0;
    while (trusted.read(buffer, sizeof(buffer), received) && received > 0)
        response.append(buffer, received);
    require(response.find("200 OK") != std::string::npos, "TLS response status");
    require(response.find("hello") != std::string::npos, "TLS response body");

    // Untrusted: a self-signed certificate must fail verification.
    TlsStream untrusted;
    require(!untrusted.connect("127.0.0.1", port, 5000), "self-signed certificate rejected without a CA");

    server.join();
    SSL_CTX_free(server_ctx);
    ::close(listen_fd);
    return 0;
}

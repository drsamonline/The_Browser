#include "aetheris/network/tls.hpp"

#include "aetheris/network/socket_abstraction.hpp"

#if !defined(AETHERIS_HAS_TLS)
#define AETHERIS_HAS_TLS 0
#endif

#if AETHERIS_HAS_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

namespace aetheris::network {

#if AETHERIS_HAS_TLS

namespace {

std::string last_openssl_error()
{
    unsigned long code = ERR_get_error();
    if (code == 0)
        return "TLS error";
    char buffer[256];
    ERR_error_string_n(code, buffer, sizeof(buffer));
    return buffer;
}

} // namespace

bool TlsStream::available() { return true; }

TlsStream::TlsStream() = default;

TlsStream::~TlsStream() { close(); }

void TlsStream::close()
{
    if (m_ssl) {
        SSL_shutdown(static_cast<SSL*>(m_ssl));
        SSL_free(static_cast<SSL*>(m_ssl));
        m_ssl = nullptr;
    }
    if (m_ctx) {
        SSL_CTX_free(static_cast<SSL_CTX*>(m_ctx));
        m_ctx = nullptr;
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

bool TlsStream::connect(std::string const& host, unsigned short port, int timeout_ms, std::string const& ca_file)
{
    close();
    OPENSSL_init_ssl(0, nullptr);
    ERR_clear_error();

    auto* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        m_error = "failed to create TLS context: " + last_openssl_error();
        return false;
    }
    m_ctx = ctx;
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_mode(ctx, SSL_MODE_AUTO_RETRY);

    if (ca_file.empty()) {
        if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
            m_error = "failed to load the system trust store";
            close();
            return false;
        }
    } else if (SSL_CTX_load_verify_locations(ctx, ca_file.c_str(), nullptr) != 1) {
        m_error = "failed to load CA file: " + last_openssl_error();
        close();
        return false;
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);

    Socket socket;
    if (socket.connect(host, port, timeout_ms) != SocketError::None) {
        m_error = "could not connect to " + host;
        close();
        return false;
    }
    m_fd = static_cast<int>(socket.release());

    auto* ssl = SSL_new(ctx);
    if (!ssl) {
        m_error = "failed to create TLS session";
        close();
        return false;
    }
    m_ssl = ssl;
    SSL_set_fd(ssl, m_fd);
    SSL_set_tlsext_host_name(ssl, host.c_str());
    SSL_set1_host(ssl, host.c_str());

    if (SSL_connect(ssl) != 1) {
        m_error = "TLS handshake failed: " + last_openssl_error();
        close();
        return false;
    }
    long verify = SSL_get_verify_result(ssl);
    if (verify != X509_V_OK) {
        m_error = std::string("certificate verification failed: ") + X509_verify_cert_error_string(verify);
        close();
        return false;
    }
    return true;
}

bool TlsStream::write(std::string_view data)
{
    if (!m_ssl) {
        m_error = "not connected";
        return false;
    }
    std::size_t written = 0;
    while (written < data.size()) {
        int result = SSL_write(static_cast<SSL*>(m_ssl), data.data() + written, static_cast<int>(data.size() - written));
        if (result <= 0) {
            m_error = "TLS write failed: " + last_openssl_error();
            return false;
        }
        written += static_cast<std::size_t>(result);
    }
    return true;
}

bool TlsStream::read(char* buffer, std::size_t capacity, std::size_t& bytes_read)
{
    bytes_read = 0;
    if (!m_ssl) {
        m_error = "not connected";
        return false;
    }
    int result = SSL_read(static_cast<SSL*>(m_ssl), buffer, static_cast<int>(capacity));
    if (result < 0) {
        m_error = "TLS read failed: " + last_openssl_error();
        return false;
    }
    bytes_read = static_cast<std::size_t>(result);
    return true;
}

#else // !AETHERIS_HAS_TLS

bool TlsStream::available() { return false; }

TlsStream::TlsStream() = default;
TlsStream::~TlsStream() = default;
void TlsStream::close() {}

bool TlsStream::connect(std::string const&, unsigned short, int, std::string const&)
{
    m_error = "TLS support is not built in";
    return false;
}

bool TlsStream::write(std::string_view)
{
    m_error = "TLS support is not built in";
    return false;
}

bool TlsStream::read(char*, std::size_t, std::size_t& bytes_read)
{
    bytes_read = 0;
    m_error = "TLS support is not built in";
    return false;
}

#endif

} // namespace aetheris::network

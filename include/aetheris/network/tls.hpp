#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace aetheris::network {

// A blocking TLS client stream. When the build is configured with OpenSSL, this
// performs a real handshake with certificate + hostname verification against the
// system trust store (or an explicit CA bundle). Otherwise `available()` is
// false and every operation fails with a clear error.
class TlsStream {
public:
    TlsStream();
    ~TlsStream();
    TlsStream(TlsStream const&) = delete;
    TlsStream& operator=(TlsStream const&) = delete;

    // Connects TCP to host:port and performs the TLS handshake. When ca_file is
    // empty the system trust store is used.
    bool connect(std::string const& host, unsigned short port, int timeout_ms = 10000, std::string const& ca_file = {});
    bool write(std::string_view data);
    bool read(char* buffer, std::size_t capacity, std::size_t& bytes_read);
    void close();

    std::string const& error() const { return m_error; }

    // True when the build has OpenSSL support compiled in.
    static bool available();

private:
    void* m_ctx { nullptr };
    void* m_ssl { nullptr };
    int m_fd { -1 };
    std::string m_error;
};

} // namespace aetheris::network

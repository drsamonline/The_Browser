#pragma once

/**
 * AETHERIS HTTP CLIENT
 * A minimal HTTP/1.1 client built on the cross-platform Socket abstraction.
 *
 * Supports http:// URLs (TLS is not implemented) and reads Content-Length,
 * chunked, and read-until-close response bodies. It exists so the browser can
 * actually load a document over the network instead of only from local files.
 */

#ifndef AETHERIS_HTTP_CLIENT_HPP
#define AETHERIS_HTTP_CLIENT_HPP

#include "aetheris/network/socket_abstraction.hpp"
#include "aetheris/network/tls.hpp"

#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace aetheris::network {

struct HttpHeader {
    std::string name;
    std::string value;
};

struct HttpResponseData {
    int status { 0 };
    std::string status_line;
    std::vector<HttpHeader> headers;
    std::string body;
    std::string error;

    [[nodiscard]] bool ok() const noexcept { return status >= 200 && status < 400; }

    [[nodiscard]] std::string header(std::string_view name) const
    {
        for (auto const& header : headers) {
            if (header.name.size() != name.size())
                continue;
            bool same = true;
            for (size_t i = 0; i < name.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(header.name[i])) != std::tolower(static_cast<unsigned char>(name[i]))) {
                    same = false;
                    break;
                }
            }
            if (same)
                return header.value;
        }
        return {};
    }
};

struct ParsedHttpUrl {
    std::string scheme;
    std::string host;
    std::string path;
    uint16_t port { 80 };
    bool valid { false };
};

inline ParsedHttpUrl parse_http_url(std::string_view url)
{
    ParsedHttpUrl out;
    auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos)
        return out;

    out.scheme = std::string(url.substr(0, scheme_end));
    for (auto& c : out.scheme)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (out.scheme != "http" && out.scheme != "https")
        return out;

    std::string_view rest = url.substr(scheme_end + 3);
    auto path_start = rest.find('/');
    std::string_view authority = rest.substr(0, path_start);
    out.path = path_start == std::string_view::npos ? std::string("/") : std::string(rest.substr(path_start));

    bool explicit_port = false;
    auto colon = authority.rfind(':');
    if (colon != std::string_view::npos) {
        out.host = std::string(authority.substr(0, colon));
        unsigned int parsed_port = 0;
        auto first = authority.data() + colon + 1;
        auto last = authority.data() + authority.size();
        auto result = std::from_chars(first, last, parsed_port);
        if (result.ec == std::errc {} && result.ptr != first) {
            out.port = static_cast<uint16_t>(parsed_port);
            explicit_port = true;
        }
    } else {
        out.host = std::string(authority);
    }

    if (!explicit_port)
        out.port = out.scheme == "https" ? 443 : 80;
    out.valid = !out.host.empty();
    return out;
}

// --- Redirect and cookie helpers -------------------------------------------

struct HttpCookie {
    std::string host;
    std::string name;
    std::string value;
};

inline std::string http_trim(std::string text)
{
    auto begin = text.find_first_not_of(" \t\r\n");
    auto end = text.find_last_not_of(" \t\r\n");
    return begin == std::string::npos ? std::string() : text.substr(begin, end - begin + 1);
}

// Resolves a (possibly relative) Location against the request URL.
inline std::string http_resolve_url(std::string const& base, std::string const& location)
{
    if (location.rfind("http://", 0) == 0 || location.rfind("https://", 0) == 0)
        return location;
    auto parsed = parse_http_url(base);
    if (!parsed.valid)
        return location;
    std::string origin = parsed.scheme + "://" + parsed.host;
    if ((parsed.scheme == "http" && parsed.port != 80) || (parsed.scheme == "https" && parsed.port != 443))
        origin += ":" + std::to_string(parsed.port);
    if (!location.empty() && location[0] == '/')
        return origin + location;
    auto slash = parsed.path.rfind('/');
    std::string directory = slash == std::string::npos ? "/" : parsed.path.substr(0, slash + 1);
    return origin + directory + location;
}

inline std::vector<HttpCookie>& http_cookie_jar()
{
    static std::vector<HttpCookie> jar;
    return jar;
}

// Records Set-Cookie headers (name=value; attributes are ignored).
inline void http_store_cookies(std::string const& host, HttpResponseData const& response)
{
    for (auto const& header : response.headers) {
        if (header.name.size() != 10)
            continue;
        std::string lower;
        for (char c : header.name)
            lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower != "set-cookie")
            continue;
        auto semicolon = header.value.find(';');
        std::string pair = header.value.substr(0, semicolon);
        auto equals = pair.find('=');
        if (equals == std::string::npos)
            continue;
        std::string name = http_trim(pair.substr(0, equals));
        std::string value = http_trim(pair.substr(equals + 1));
        if (name.empty())
            continue;
        auto& jar = http_cookie_jar();
        bool replaced = false;
        for (auto& cookie : jar)
            if (cookie.host == host && cookie.name == name) {
                cookie.value = value;
                replaced = true;
            }
        if (!replaced)
            jar.push_back({ host, name, value });
    }
}

inline std::string http_cookie_header(std::string const& host)
{
    std::string result;
    for (auto const& cookie : http_cookie_jar()) {
        if (cookie.host != host)
            continue;
        if (!result.empty())
            result += "; ";
        result += cookie.name + "=" + cookie.value;
    }
    return result;
}

// A minimal send/recv transport over either a plain socket or a TLS stream.
struct HttpTransport {
    Socket* socket { nullptr };
    TlsStream* tls { nullptr };

    SocketError send(std::string_view data)
    {
        if (tls)
            return tls->write(data) ? SocketError::None : SocketError::SendFailed;
        return socket->send(data);
    }

    SocketError recv(std::span<char> buffer, size_t& bytes_read)
    {
        if (tls)
            return tls->read(buffer.data(), buffer.size(), bytes_read) ? SocketError::None : SocketError::RecvFailed;
        return socket->recv(buffer, bytes_read);
    }
};

class HttpClient {
public:
    static HttpResponseData get(std::string const& url, int timeout_ms = 8000)
    {
        return request(url, "GET", {}, {}, timeout_ms);
    }

    // Follows up to five redirects (301/302/303/307/308).
    static HttpResponseData request(std::string const& url, std::string const& method,
        std::vector<HttpHeader> headers, std::string const& body, int timeout_ms = 8000)
    {
        std::string current = url;
        std::string current_method = method;
        std::string current_body = body;
        for (int hop = 0; hop < 6; ++hop) {
            auto response = request_once(current, current_method, headers, current_body, timeout_ms);
            if (!response.error.empty())
                return response;
            if (response.status == 301 || response.status == 302 || response.status == 303 || response.status == 307 || response.status == 308) {
                auto location = response.header("Location");
                if (location.empty())
                    return response;
                current = http_resolve_url(current, location);
                if (response.status == 303) {
                    current_method = "GET";
                    current_body.clear();
                }
                continue;
            }
            return response;
        }
        HttpResponseData response;
        response.error = "Too many redirects";
        return response;
    }

    static void clear_cookies() { http_cookie_jar().clear(); }

private:
    static HttpResponseData request_once(std::string const& url, std::string const& method,
        std::vector<HttpHeader> headers, std::string const& body, int timeout_ms)
    {
        HttpResponseData response;

        auto parsed = parse_http_url(url);
        if (!parsed.valid) {
            response.error = "Invalid or unsupported URL";
            return response;
        }
        Socket socket;
        TlsStream tls;
        HttpTransport transport;
        if (parsed.scheme == "https") {
            if (!TlsStream::available()) {
                response.error = "TLS support is not built in";
                return response;
            }
            // AETHERIS_CA_FILE lets a test or a corporate proxy trust an
            // explicit CA bundle; otherwise the system store is used.
            auto const* ca_file = std::getenv("AETHERIS_CA_FILE");
            if (!tls.connect(parsed.host, parsed.port, timeout_ms, ca_file ? ca_file : "")) {
                response.error = tls.error();
                return response;
            }
            transport.tls = &tls;
        } else {
            if (socket.connect(parsed.host, parsed.port, timeout_ms) != SocketError::None) {
                response.error = "Could not connect to " + parsed.host;
                return response;
            }
            transport.socket = &socket;
        }

        std::string request_text = method + " " + parsed.path + " HTTP/1.1\r\n";
        request_text += "Host: " + parsed.host + "\r\n";
        request_text += "User-Agent: Aetheris/1.0\r\n";
        request_text += "Accept: */*\r\n";
        auto cookie_header = http_cookie_header(parsed.host);
        if (!cookie_header.empty())
            request_text += "Cookie: " + cookie_header + "\r\n";
        request_text += "Connection: close\r\n";
        for (auto const& header : headers)
            request_text += header.name + ": " + header.value + "\r\n";
        if (!body.empty())
            request_text += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        request_text += "\r\n";
        request_text += body;

        if (transport.send(request_text) != SocketError::None) {
            response.error = "Failed to send request";
            return response;
        }

        std::string raw;
        if (!read_headers(transport, raw, response.error))
            return response;

        auto header_end = raw.find("\r\n\r\n");
        if (header_end == std::string::npos) {
            response.error = "Malformed response";
            return response;
        }

        auto metadata = HttpSniffer::parse_response_headers(raw.substr(0, header_end + 4));
        response.status_line = metadata.status_line;
        for (auto const& header : metadata.headers)
            response.headers.push_back({ header.first, header.second });

        auto space = response.status_line.find(' ');
        if (space != std::string::npos) {
            int code = 0;
            auto first = response.status_line.data() + space + 1;
            auto last = response.status_line.data() + response.status_line.size();
            auto result = std::from_chars(first, last, code);
            if (result.ec == std::errc {} && result.ptr != first)
                response.status = code;
        }

        std::string remainder = raw.substr(header_end + 4);
        if (lower_equals(response.header("Transfer-Encoding"), "chunked")) {
            read_remaining(transport, remainder);
            response.body = decode_chunked(remainder);
        } else {
            auto content_length = response.header("Content-Length");
            if (!content_length.empty()) {
                auto wanted = parse_size(content_length);
                read_until(transport, remainder, wanted);
                response.body = remainder.size() > wanted ? remainder.substr(0, wanted) : remainder;
            } else {
                read_remaining(transport, remainder);
                response.body = remainder;
            }
        }

        http_store_cookies(parsed.host, response);
        return response;
    }

    static bool lower_equals(std::string const& value, std::string_view expected)
    {
        if (value.size() != expected.size())
            return false;
        for (size_t i = 0; i < value.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(value[i])) != std::tolower(static_cast<unsigned char>(expected[i])))
                return false;
        }
        return true;
    }

    static size_t parse_size(std::string const& text)
    {
        unsigned long long value = 0;
        auto first = text.data();
        auto last = text.data() + text.size();
        auto result = std::from_chars(first, last, value);
        return result.ec == std::errc {} ? static_cast<size_t>(value) : 0;
    }

    static bool read_headers(HttpTransport& transport, std::string& out, std::string& error)
    {
        char buffer[4096];
        while (out.find("\r\n\r\n") == std::string::npos) {
            size_t bytes = 0;
            if (transport.recv({ buffer, sizeof(buffer) }, bytes) != SocketError::None) {
                error = "Failed while reading response";
                return false;
            }
            if (bytes == 0)
                break;
            out.append(buffer, bytes);
            if (out.size() > 256 * 1024 && out.find("\r\n\r\n") == std::string::npos) {
                error = "Response headers too large";
                return false;
            }
        }
        return true;
    }

    static void read_until(HttpTransport& transport, std::string& out, size_t wanted)
    {
        char buffer[4096];
        while (out.size() < wanted) {
            size_t bytes = 0;
            if (transport.recv({ buffer, sizeof(buffer) }, bytes) != SocketError::None)
                break;
            if (bytes == 0)
                break;
            out.append(buffer, bytes);
        }
    }

    static void read_remaining(HttpTransport& transport, std::string& out)
    {
        char buffer[4096];
        for (;;) {
            size_t bytes = 0;
            if (transport.recv({ buffer, sizeof(buffer) }, bytes) != SocketError::None)
                break;
            if (bytes == 0)
                break;
            out.append(buffer, bytes);
        }
    }

    static std::string decode_chunked(std::string const& data)
    {
        std::string out;
        size_t pos = 0;
        while (pos < data.size()) {
            auto line_end = data.find("\r\n", pos);
            if (line_end == std::string::npos)
                break;
            size_t chunk_size = 0;
            auto first = data.data() + pos;
            auto last = data.data() + line_end;
            auto result = std::from_chars(first, last, chunk_size, 16);
            if (result.ec != std::errc {})
                break;
            pos = line_end + 2;
            if (chunk_size == 0)
                break;
            if (pos + chunk_size > data.size())
                chunk_size = data.size() - pos;
            out.append(data, pos, chunk_size);
            pos += chunk_size;
            if (data.compare(pos, 2, "\r\n") == 0)
                pos += 2;
        }
        return out;
    }
};

} // namespace aetheris::network

#endif // AETHERIS_HTTP_CLIENT_HPP

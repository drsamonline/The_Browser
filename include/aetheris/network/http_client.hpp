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
#include <chrono>
#include <memory>
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
    std::string host;      // host that set it (or the Domain attribute)
    std::string name;
    std::string value;
    std::string path { "/" };
    std::string domain;    // empty unless the Domain attribute was present
    std::int64_t expires { 0 }; // 0 = session cookie
    bool secure { false };
    bool http_only { false };
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

// Defined with the response cache below; the cookie jar needs it for expiry.
inline std::int64_t http_now_seconds();

inline std::string http_lower_copy(std::string text)
{
    for (auto& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// Parses an HTTP date (RFC 1123, RFC 850 or asctime) into epoch seconds.
inline std::int64_t http_parse_cookie_date(std::string const& text)
{
    static char const* months[] = { "jan", "feb", "mar", "apr", "may", "jun",
        "jul", "aug", "sep", "oct", "nov", "dec" };
    std::string lowered = http_lower_copy(text);

    int day = 0, month = -1, year = 0;
    int hour = 0, minute = 0, second = 0;

    // Scan tokens; a "12:34:56" token is the time, a month name sets the month.
    std::string token;
    auto flush = [&] {
        if (token.empty())
            return;
        if (token.find(':') != std::string::npos) {
            std::sscanf(token.c_str(), "%d:%d:%d", &hour, &minute, &second);
        } else if (month < 0) {
            for (int index = 0; index < 12; ++index) {
                if (token.rfind(months[index], 0) == 0) {
                    month = index;
                    break;
                }
            }
            if (month < 0) {
                int value = std::atoi(token.c_str());
                if (value > 31)
                    year = value; // four-digit year
                else if (value > 0)
                    day = value;
            }
        } else {
            int value = std::atoi(token.c_str());
            if (value > 31)
                year = value;
            else if (value > 0)
                day = value;
        }
        token.clear();
    };
    for (char c : lowered) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == ':')
            token += c;
        else
            flush();
    }
    flush();

    if (month < 0 || day <= 0 || year <= 0)
        return 0;
    if (year < 100)
        year += year < 70 ? 2000 : 1900;

    // Days since the epoch (Howard Hinnant's civil_from_days, inverted).
    int y = year;
    int m = month + 1;
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = static_cast<unsigned>(y - era * 400);
    unsigned doy = static_cast<unsigned>((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + day - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = static_cast<long>(era) * 146097 + static_cast<long>(doe) - 719468;
    return static_cast<std::int64_t>(days) * 86400 + hour * 3600 + minute * 60 + second;
}

// Records Set-Cookie headers, honouring Domain, Path, Expires, Max-Age, Secure
// and HttpOnly.
inline void http_store_cookies(std::string const& host, HttpResponseData const& response)
{
    for (auto const& header : response.headers) {
        if (http_lower_copy(header.name) != "set-cookie")
            continue;

        auto semicolon = header.value.find(';');
        std::string pair = header.value.substr(0, semicolon);
        auto equals = pair.find('=');
        if (equals == std::string::npos)
            continue;

        HttpCookie cookie;
        cookie.host = host;
        cookie.name = http_trim(pair.substr(0, equals));
        cookie.value = http_trim(pair.substr(equals + 1));
        if (cookie.name.empty())
            continue;

        std::string attributes = semicolon == std::string::npos ? std::string() : header.value.substr(semicolon + 1);
        std::size_t start = 0;
        while (start < attributes.size()) {
            auto next = attributes.find(';', start);
            std::string attribute = http_trim(attributes.substr(start, next == std::string::npos ? std::string::npos : next - start));
            start = next == std::string::npos ? attributes.size() : next + 1;
            if (attribute.empty())
                continue;
            auto attribute_equals = attribute.find('=');
            std::string key = http_lower_copy(http_trim(attribute.substr(0, attribute_equals)));
            std::string value = attribute_equals == std::string::npos
                ? std::string()
                : http_trim(attribute.substr(attribute_equals + 1));

            if (key == "domain" && !value.empty()) {
                if (!value.empty() && value.front() == '.')
                    value.erase(value.begin());
                cookie.domain = http_lower_copy(value);
            } else if (key == "path" && !value.empty()) {
                cookie.path = value;
            } else if (key == "expires") {
                cookie.expires = http_parse_cookie_date(value);
            } else if (key == "max-age") {
                auto seconds = std::strtoll(value.c_str(), nullptr, 10);
                if (seconds <= 0)
                    cookie.expires = 1; // already expired
                else
                    cookie.expires = http_now_seconds() + seconds;
            } else if (key == "secure") {
                cookie.secure = true;
            } else if (key == "httponly") {
                cookie.http_only = true;
            }
        }

        auto& jar = http_cookie_jar();
        for (auto it = jar.begin(); it != jar.end();) {
            if (it->name == cookie.name && it->host == cookie.host && it->path == cookie.path)
                it = jar.erase(it);
            else
                ++it;
        }
        // An expired cookie deletes rather than stores.
        if (cookie.expires == 1)
            continue;
        jar.push_back(cookie);
    }
}

inline bool http_host_matches_domain(std::string const& host, std::string const& domain)
{
    if (domain.empty())
        return false;
    if (host == domain)
        return true;
    if (host.size() > domain.size() && host.compare(host.size() - domain.size(), domain.size(), domain) == 0) {
        auto dot = host.size() - domain.size() - 1;
        return dot < host.size() && host[dot] == '.';
    }
    return false;
}

// Builds the Cookie header for a request, honouring domain, path, expiry and
// the Secure flag.
inline std::string http_cookie_header(std::string const& host, std::string const& path = "/", bool secure = false)
{
    std::string result;
    auto now = http_now_seconds();
    for (auto const& cookie : http_cookie_jar()) {
        if (cookie.expires != 0 && cookie.expires <= now)
            continue;
        bool host_ok = cookie.domain.empty() ? cookie.host == host : http_host_matches_domain(host, cookie.domain);
        if (!host_ok)
            continue;
        // Path match: exact, or a prefix ending on a boundary.
        if (cookie.path != "/" && path.rfind(cookie.path, 0) != 0)
            continue;
        if (cookie.secure && !secure)
            continue;
        if (!result.empty())
            result += "; ";
        result += cookie.name + "=" + cookie.value;
    }
    return result;
}

// --- Response cache --------------------------------------------------------

struct HttpCacheEntry {
    std::string url;
    HttpResponseData response;
    std::int64_t expiry { 0 };
};

inline std::vector<HttpCacheEntry>& http_cache_entries()
{
    static std::vector<HttpCacheEntry> entries;
    return entries;
}

inline std::int64_t http_now_seconds()
{
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

// Returns the max-age in seconds from Cache-Control, or -1 when absent.
inline std::int64_t http_max_age(HttpResponseData const& response)
{
    auto control = response.header("Cache-Control");
    auto position = control.find("max-age=");
    if (position == std::string::npos)
        return -1;
    position += 8;
    std::int64_t value = 0;
    bool digits = false;
    while (position < control.size() && std::isdigit(static_cast<unsigned char>(control[position]))) {
        value = value * 10 + (control[position] - '0');
        ++position;
        digits = true;
    }
    return digits ? value : -1;
}

// --- Connection pool -------------------------------------------------------

struct HttpConnection {
    std::unique_ptr<Socket> socket;
    std::unique_ptr<TlsStream> tls;
};

inline std::vector<std::pair<std::string, HttpConnection>>& http_connection_pool()
{
    static std::vector<std::pair<std::string, HttpConnection>> pool;
    return pool;
}

inline std::string http_origin_key(ParsedHttpUrl const& url)
{
    return url.scheme + "://" + url.host + ":" + std::to_string(url.port);
}

inline HttpConnection http_take_connection(std::string const& key)
{
    auto& pool = http_connection_pool();
    for (auto it = pool.begin(); it != pool.end(); ++it) {
        if (it->first == key) {
            HttpConnection connection = std::move(it->second);
            pool.erase(it);
            return connection;
        }
    }
    return {};
}

// Keeps at most four idle connections per origin.
inline void http_return_connection(std::string const& key, HttpConnection connection)
{
    auto& pool = http_connection_pool();
    std::size_t count = 0;
    for (auto const& entry : pool)
        if (entry.first == key)
            ++count;
    if (count >= 4)
        return;
    pool.emplace_back(key, std::move(connection));
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
    static void clear_cache() { http_cache_entries().clear(); }
    static void clear_connections() { http_connection_pool().clear(); }

private:
    static HttpResponseData request_once(std::string const& url, std::string const& method,
        std::vector<HttpHeader> headers, std::string const& body, int timeout_ms)
    {
        HttpResponseData response;

        // Serve a fresh cached response when the server allowed it.
        auto now = http_now_seconds();
        for (auto const& entry : http_cache_entries())
            if (entry.url == url && entry.expiry > now)
                return entry.response;

        auto parsed = parse_http_url(url);
        if (!parsed.valid) {
            response.error = "Invalid or unsupported URL";
            return response;
        }
        auto const key = http_origin_key(parsed);
        HttpConnection connection = http_take_connection(key);

        // A pooled keep-alive connection can be closed by the server while
        // idle. Probe it before deciding whether to reconnect, otherwise the
        // request is written into a dead (or already-reset) socket.
        if (connection.socket && !connection.socket->is_alive()) {
            connection.socket.reset();
            connection.tls.reset();
        }

        if (!connection.socket && !connection.tls) {
            if (parsed.scheme == "https") {
                if (!TlsStream::available()) {
                    response.error = "TLS support is not built in";
                    return response;
                }
                // AETHERIS_CA_FILE lets a test or a corporate proxy trust an
                // explicit CA bundle; otherwise the system store is used.
                auto const* ca_file = std::getenv("AETHERIS_CA_FILE");
                auto tls = std::make_unique<TlsStream>();
                if (!tls->connect(parsed.host, parsed.port, timeout_ms, ca_file ? ca_file : "")) {
                    response.error = tls->error();
                    return response;
                }
                connection.tls = std::move(tls);
            } else {
                auto socket = std::make_unique<Socket>();
                if (socket->connect(parsed.host, parsed.port, timeout_ms) != SocketError::None) {
                    response.error = "Could not connect to " + parsed.host;
                    return response;
                }
                connection.socket = std::move(socket);
            }
        }

        HttpTransport transport;
        transport.socket = connection.socket.get();
        transport.tls = connection.tls.get();

        std::string request_text = method + " " + parsed.path + " HTTP/1.1\r\n";
        request_text += "Host: " + parsed.host + "\r\n";
        request_text += "User-Agent: Aetheris/1.0\r\n";
        request_text += "Accept: */*\r\n";
        auto cookie_header = http_cookie_header(parsed.host, parsed.path, parsed.scheme == "https");
        if (!cookie_header.empty())
            request_text += "Cookie: " + cookie_header + "\r\n";
        request_text += "Connection: keep-alive\r\n";
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

        // Keep the connection only when the body was fully delimited and the
        // server did not ask to close it.
        {
            std::string connection_header = response.header("Connection");
            for (auto& c : connection_header)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            bool server_close = connection_header.find("close") != std::string::npos;
            bool delimited = !response.header("Content-Length").empty()
                || lower_equals(response.header("Transfer-Encoding"), "chunked");
            if (delimited && !server_close)
                http_return_connection(key, std::move(connection));
        }

        auto max_age = http_max_age(response);
        if (max_age > 0) {
            auto& entries = http_cache_entries();
            auto expiry = http_now_seconds() + max_age;
            bool replaced = false;
            for (auto& entry : entries)
                if (entry.url == url) {
                    entry.response = response;
                    entry.expiry = expiry;
                    replaced = true;
                }
            if (!replaced)
                entries.push_back({ url, response, expiry });
        }
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

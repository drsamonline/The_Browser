#pragma once

#include <cctype>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aetheris::network {

// A proxy a request can be sent through (row 0.3).
//
// Upstream keeps this as `HTTP::Proxy` with a type, a host, a port and the userinfo from the
// environment variable (LibHTTP/Proxy.h), parsed once from the variable's text
// (LibHTTP/Proxy.cpp:18) and selected per URL by `ProxyConfiguration::proxy_for`
// (LibHTTP/Proxy.cpp:186). This is the same shape at this engine's scale: plain HTTP and HTTPS
// proxies, no SOCKS - a SOCKS proxy needs a different handshake and nothing here needs one yet.
//
// Credentials are kept in `userinfo` and are never put in a log line or an error: `describe()`
// is what callers print.
struct ProxyEndpoint {
    enum class Type : unsigned char {
        Http,  // http://proxy:8080 - the connection to the proxy is plain, whatever the origin is
        Https, // https://proxy:8443 - the connection to the proxy is itself TLS (not used yet)
    };

    Type type { Type::Http };
    std::string host;
    unsigned short port { 0 };
    // `user:password` as written in the variable, still percent-encoded. Empty when absent.
    std::string userinfo;

    // "http://host:port", never the credentials.
    std::string describe() const
    {
        return std::string(type == Type::Https ? "https://" : "http://") + host + ":" + std::to_string(port);
    }

    // The `Proxy-Authorization` header value, or empty when the variable carried no userinfo.
    // Basic is the only scheme an HTTP proxy reliably accepts, which is what curl sends.
    std::string authorization_header() const
    {
        if (userinfo.empty())
            return {};
        return "Basic " + base64_encode(percent_decode(userinfo));
    }

    // `[http://][user:pass@]host[:port]`. Returns nothing for anything this engine cannot use,
    // which is how an unparseable variable is ignored rather than treated as "no proxy".
    static std::optional<ProxyEndpoint> parse(std::string_view text)
    {
        text = trim(text);
        if (text.empty())
            return {};

        ProxyEndpoint endpoint;

        if (auto const scheme_end = text.find("://"); scheme_end != std::string_view::npos) {
            auto const scheme = text.substr(0, scheme_end);
            if (equals_ignoring_case(scheme, "http"))
                endpoint.type = Type::Http;
            else if (equals_ignoring_case(scheme, "https"))
                endpoint.type = Type::Https;
            else
                return {}; // socks4/5 are not implemented, so they are not silently treated as http
            text = text.substr(scheme_end + 3);
        }

        if (auto const path_start = text.find('/'); path_start != std::string_view::npos)
            text = text.substr(0, path_start);

        if (auto const at = text.rfind('@'); at != std::string_view::npos) {
            endpoint.userinfo = std::string(text.substr(0, at));
            text = text.substr(at + 1);
        }

        std::optional<std::string_view> port_text;
        if (!text.empty() && text.front() == '[') {
            auto const close = text.find(']');
            if (close == std::string_view::npos)
                return {};
            endpoint.host = lower(text.substr(1, close - 1));
            auto const rest = text.substr(close + 1);
            if (!rest.empty()) {
                if (rest.front() != ':')
                    return {};
                port_text = rest.substr(1);
            }
        } else if (auto const colon = text.rfind(':'); colon != std::string_view::npos) {
            auto const host = text.substr(0, colon);
            // An IPv6 address has to be bracketed to tell its colons from the port separator.
            if (host.find(':') != std::string_view::npos)
                return {};
            endpoint.host = lower(host);
            port_text = text.substr(colon + 1);
        } else {
            endpoint.host = lower(text);
        }

        if (endpoint.host.empty())
            return {};

        if (port_text) {
            if (port_text->empty())
                return {};
            unsigned long value = 0;
            for (char const c : *port_text) {
                if (!std::isdigit(static_cast<unsigned char>(c)))
                    return {};
                value = value * 10 + static_cast<unsigned long>(c - '0');
                if (value > 65535)
                    return {};
            }
            if (value == 0)
                return {};
            endpoint.port = static_cast<unsigned short>(value);
        } else {
            // libcurl's default proxy ports, as upstream uses (LibHTTP/Proxy.cpp:85).
            endpoint.port = endpoint.type == Type::Https ? 443 : 1080;
        }
        return endpoint;
    }

private:
    static std::string_view trim(std::string_view text)
    {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
            text.remove_prefix(1);
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
            text.remove_suffix(1);
        return text;
    }

    static bool equals_ignoring_case(std::string_view a, std::string_view b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        return true;
    }

    static std::string lower(std::string_view text)
    {
        std::string out(text);
        for (auto& c : out)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }

    // `user%40name:p%3Ass` has to reach the proxy as `user@name:p:ss`, because that is what the
    // origin will compare against.
    static std::string percent_decode(std::string_view text)
    {
        std::string out;
        out.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '%' && i + 2 < text.size()) {
                auto const hex = [](char c) -> int {
                    if (c >= '0' && c <= '9')
                        return c - '0';
                    if (c >= 'a' && c <= 'f')
                        return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F')
                        return c - 'A' + 10;
                    return -1;
                };
                int const high = hex(text[i + 1]);
                int const low = hex(text[i + 2]);
                if (high >= 0 && low >= 0) {
                    out.push_back(static_cast<char>((high << 4) | low));
                    i += 2;
                    continue;
                }
            }
            out.push_back(text[i]);
        }
        return out;
    }

    static std::string base64_encode(std::string_view input)
    {
        static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve((input.size() + 2) / 3 * 4);
        std::size_t i = 0;
        while (i + 2 < input.size()) {
            auto const chunk = (static_cast<unsigned>(static_cast<unsigned char>(input[i])) << 16)
                | (static_cast<unsigned>(static_cast<unsigned char>(input[i + 1])) << 8)
                | static_cast<unsigned>(static_cast<unsigned char>(input[i + 2]));
            out.push_back(alphabet[(chunk >> 18) & 0x3f]);
            out.push_back(alphabet[(chunk >> 12) & 0x3f]);
            out.push_back(alphabet[(chunk >> 6) & 0x3f]);
            out.push_back(alphabet[chunk & 0x3f]);
            i += 3;
        }
        auto const remaining = input.size() - i;
        if (remaining == 1) {
            auto const chunk = static_cast<unsigned>(static_cast<unsigned char>(input[i])) << 16;
            out.push_back(alphabet[(chunk >> 18) & 0x3f]);
            out.push_back(alphabet[(chunk >> 12) & 0x3f]);
            out.push_back('=');
            out.push_back('=');
        } else if (remaining == 2) {
            auto const chunk = (static_cast<unsigned>(static_cast<unsigned char>(input[i])) << 16)
                | (static_cast<unsigned>(static_cast<unsigned char>(input[i + 1])) << 8);
            out.push_back(alphabet[(chunk >> 18) & 0x3f]);
            out.push_back(alphabet[(chunk >> 12) & 0x3f]);
            out.push_back(alphabet[(chunk >> 6) & 0x3f]);
            out.push_back('=');
        }
        return out;
    }
};

// The proxy settings, read from the environment the way libcurl reads them.
//
// Precedence and names follow upstream (LibHTTP/Proxy.cpp:149): `https_proxy`/`HTTPS_PROXY` for an
// https request, `http_proxy` for an http one, `all_proxy`/`ALL_PROXY` for both, and
// `no_proxy`/`NO_PROXY` as the exclusion list. `HTTP_PROXY` is deliberately not read: it is the one
// a CGI environment can populate from a request header, which is why upstream ignores it too.
class ProxyConfiguration {
public:
    using EnvironmentLookup = std::function<std::optional<std::string>(std::string_view)>;

    static ProxyConfiguration from_environment()
    {
        return from_lookup([](std::string_view name) -> std::optional<std::string> {
            if (auto const* value = std::getenv(std::string(name).c_str()))
                return std::string(value);
            return {};
        });
    }

    static ProxyConfiguration from_lookup(EnvironmentLookup const& lookup)
    {
        auto first_set = [&](std::initializer_list<std::string_view> names) -> std::optional<std::string> {
            for (auto const name : names) {
                auto value = lookup(name);
                if (value && !trim_view(*value).empty())
                    return value;
            }
            return {};
        };
        auto proxy_from = [&](std::initializer_list<std::string_view> names) -> std::optional<ProxyEndpoint> {
            if (auto const value = first_set(names))
                return ProxyEndpoint::parse(*value);
            return {};
        };

        ProxyConfiguration configuration;
        auto const all_proxy = proxy_from({ "all_proxy", "ALL_PROXY" });
        configuration.m_http_proxy = proxy_from({ "http_proxy" });
        configuration.m_https_proxy = proxy_from({ "https_proxy", "HTTPS_PROXY" });
        if (!configuration.m_http_proxy && all_proxy)
            configuration.m_http_proxy = all_proxy;
        if (!configuration.m_https_proxy && all_proxy)
            configuration.m_https_proxy = all_proxy;

        if (auto const value = first_set({ "no_proxy", "NO_PROXY" })) {
            std::string_view remaining = trim_view(*value);
            while (!remaining.empty()) {
                auto const comma = remaining.find(',');
                auto const piece = remaining.substr(0, comma);
                remaining = comma == std::string_view::npos ? std::string_view {} : remaining.substr(comma + 1);
                auto const entry = trim_view(piece);
                if (!entry.empty())
                    configuration.m_no_proxy.emplace_back(entry);
            }
        }
        return configuration;
    }

    // The proxy for a request to `host` over `scheme`, or nothing when there is none, when the
    // scheme is not proxied, or when `no_proxy` excludes the host.
    std::optional<ProxyEndpoint> proxy_for(std::string_view scheme, std::string_view host) const
    {
        std::optional<ProxyEndpoint> const* candidate = nullptr;
        if (scheme == "http" || scheme == "ws")
            candidate = &m_http_proxy;
        else if (scheme == "https" || scheme == "wss")
            candidate = &m_https_proxy;
        if (!candidate || !candidate->has_value())
            return {};
        if (host.empty() || is_excluded(host))
            return {};
        return **candidate;
    }

    bool empty() const { return !m_http_proxy.has_value() && !m_https_proxy.has_value(); }
    std::optional<ProxyEndpoint> const& http_proxy() const { return m_http_proxy; }
    std::optional<ProxyEndpoint> const& https_proxy() const { return m_https_proxy; }
    std::vector<std::string> const& no_proxy() const { return m_no_proxy; }

    // `no_proxy` matching, as upstream implements it (LibHTTP/Proxy.cpp:252): `*` excludes
    // everything; a bare name matches that host; `.example.test` matches it and every subdomain.
    // A port in the entry is ignored, which is what libcurl does with one.
    bool is_excluded(std::string_view host) const
    {
        host = trim_view(host);
        if (host.size() >= 2 && host.front() == '[' && host.back() == ']')
            host = host.substr(1, host.size() - 2);
        if (!host.empty() && host.back() == '.')
            host.remove_suffix(1);
        auto const lowered_host = lower_view(host);

        for (auto const& raw_entry : m_no_proxy) {
            std::string_view entry = trim_view(raw_entry);
            if (entry == "*")
                return true;
            // A port is not part of the comparison.
            if (auto const colon = entry.rfind(':'); colon != std::string_view::npos
                && entry.find(':') == colon)
                entry = entry.substr(0, colon);
            if (!entry.empty() && entry.front() == '.')
                entry.remove_prefix(1);
            if (!entry.empty() && entry.back() == '.')
                entry.remove_suffix(1);
            if (entry.empty())
                continue;
            auto const lowered_entry = lower_view(entry);
            if (lowered_host == lowered_entry)
                return true;
            if (lowered_host.size() > lowered_entry.size()
                && lowered_host[lowered_host.size() - lowered_entry.size() - 1] == '.'
                && lowered_host.compare(lowered_host.size() - lowered_entry.size(), lowered_entry.size(), lowered_entry) == 0)
                return true;
        }
        return false;
    }

private:
    static std::string_view trim_view(std::string_view text)
    {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
            text.remove_prefix(1);
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
            text.remove_suffix(1);
        return text;
    }

    static std::string lower_view(std::string_view text)
    {
        std::string out(text);
        for (auto& c : out)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }

    std::optional<ProxyEndpoint> m_http_proxy;
    std::optional<ProxyEndpoint> m_https_proxy;
    std::vector<std::string> m_no_proxy;
};

} // namespace aetheris::network

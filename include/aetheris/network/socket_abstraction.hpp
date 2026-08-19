#pragma once

/**
 * AETHERIS SOCKET ABSTRACTION LAYER
 * Cross-platform network stream sniffer with HTML/CSS interception
 * Windows (Winsock2) and Unix-like systems via clean #ifdef directives
 */

#ifndef AETHERIS_SOCKET_ABSTRACTION_HPP
#define AETHERIS_SOCKET_ABSTRACTION_HPP

#include "aetheris/core.hpp"
#include <string>
#include <string_view>
#include <optional>
#include <functional>
#include <vector>
#include <span>

#if AETHERIS_PLATFORM_WINDOWS
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
#endif

namespace aetheris::network {

// Socket error type for cross-platform error handling
enum class SocketError : int32_t {
    None = 0,
    InvalidSocket = -1,
    ConnectionFailed = -2,
    SendFailed = -3,
    RecvFailed = -4,
    Timeout = -5,
    NotInitialized = -6
};

// Content type detection for bypass logic
enum class ContentType : uint8_t {
    Unknown = 0,
    HTML,
    CSS,
    JavaScript,
    Binary,
    JSON,
    Other
};

// HTTP Response metadata
struct ResponseMetadata {
    ContentType content_type{ContentType::Unknown};
    size_t content_length{0};
    bool has_javascript{false};
    std::string status_line;
    std::vector<std::pair<std::string, std::string>> headers;
};

/**
 * Cross-platform socket wrapper
 * Provides raw TCP socket functionality with platform-specific implementations
 */
class Socket {
public:
    Socket() noexcept = default;
    
    ~Socket() noexcept {
        close();
    }
    
    // Non-copyable, movable
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : m_socket(other.m_socket) {
        other.m_socket = INVALID_SOCKET_VALUE;
    }
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            close();
            m_socket = other.m_socket;
            other.m_socket = INVALID_SOCKET_VALUE;
        }
        return *this;
    }
    
    /**
     * Initialize socket subsystem (required on Windows)
     */
    static SocketError initialize() noexcept {
#if AETHERIS_PLATFORM_WINDOWS
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            return SocketError::NotInitialized;
        }
#endif
        return SocketError::None;
    }
    
    /**
     * Cleanup socket subsystem (required on Windows)
     */
    static void cleanup() noexcept {
#if AETHERIS_PLATFORM_WINDOWS
        WSACleanup();
#endif
    }
    
    /**
     * Connect to remote host
     */
    SocketError connect(std::string_view host, uint16_t port, int timeout_ms = 5000) {
        // Create socket
#if AETHERIS_PLATFORM_WINDOWS
        m_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_socket == INVALID_SOCKET) {
            return SocketError::InvalidSocket;
        }
        
        // Set timeout
        DWORD timeout = static_cast<DWORD>(timeout_ms);
        ::setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, 
                     reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
        m_socket = ::socket(AF_INET, SOCK_STREAM, 0);
        if (m_socket < 0) {
            return SocketError::InvalidSocket;
        }
        
        // Set timeout using timeval
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ::setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
        
        // Resolve hostname
        struct sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        
#if AETHERIS_PLATFORM_WINDOWS
        auto addr_result = inet_pton(AF_INET, std::string(host).c_str(), &server_addr.sin_addr);
#else
        auto addr_result = inet_pton(AF_INET, std::string(host).c_str(), &server_addr.sin_addr);
#endif
        
        if (addr_result <= 0) {
            // Try DNS resolution (simplified - in production use getaddrinfo)
            return SocketError::ConnectionFailed;
        }
        
        // Connect
#if AETHERIS_PLATFORM_WINDOWS
        int result = ::connect(m_socket, 
                               reinterpret_cast<struct sockaddr*>(&server_addr), 
                               sizeof(server_addr));
        if (result == SOCKET_ERROR) {
            close();
            return SocketError::ConnectionFailed;
        }
#else
        int result = ::connect(m_socket, 
                               reinterpret_cast<struct sockaddr*>(&server_addr), 
                               sizeof(server_addr));
        if (result < 0) {
            close();
            return SocketError::ConnectionFailed;
        }
#endif
        
        return SocketError::None;
    }
    
    /**
     * Send data over socket
     */
    SocketError send(std::string_view data) {
        if (!is_valid()) {
            return SocketError::InvalidSocket;
        }
        
#if AETHERIS_PLATFORM_WINDOWS
        int result = ::send(m_socket, data.data(), static_cast<int>(data.size()), 0);
        if (result == SOCKET_ERROR || result != static_cast<int>(data.size())) {
            return SocketError::SendFailed;
        }
#else
        ssize_t result = ::send(m_socket, data.data(), data.size(), 0);
        if (result < 0 || result != static_cast<ssize_t>(data.size())) {
            return SocketError::SendFailed;
        }
#endif
        
        return SocketError::None;
    }
    
    /**
     * Receive data from socket
     */
    SocketError recv(std::span<char> buffer, size_t& bytes_read) {
        if (!is_valid()) {
            return SocketError::InvalidSocket;
        }
        
#if AETHERIS_PLATFORM_WINDOWS
        int result = ::recv(m_socket, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (result == SOCKET_ERROR) {
            return SocketError::RecvFailed;
        }
        bytes_read = static_cast<size_t>(result);
#else
        ssize_t result = ::recv(m_socket, buffer.data(), buffer.size(), 0);
        if (result < 0) {
            return SocketError::RecvFailed;
        }
        bytes_read = static_cast<size_t>(result);
#endif
        
        return SocketError::None;
    }
    
    /**
     * Close socket connection
     */
    void close() noexcept {
        if (is_valid()) {
#if AETHERIS_PLATFORM_WINDOWS
            ::closesocket(m_socket);
#else
            ::close(m_socket);
#endif
            m_socket = INVALID_SOCKET_VALUE;
        }
    }
    
    /**
     * Check if socket is valid
     */
    bool is_valid() const noexcept {
#if AETHERIS_PLATFORM_WINDOWS
        return m_socket != INVALID_SOCKET;
#else
        return m_socket >= 0;
#endif
    }
    
private:
#if AETHERIS_PLATFORM_WINDOWS
    static constexpr SOCKET INVALID_SOCKET_VALUE = INVALID_SOCKET;
#else
    static constexpr int INVALID_SOCKET_VALUE = -1;
#endif
    
    decltype(INVALID_SOCKET_VALUE) m_socket{INVALID_SOCKET_VALUE};
};

/**
 * HTTP Stream Sniffer
 * Intercepts incoming HTML/CSS and determines if JS engine can be bypassed
 */
class HttpSniffer {
public:
    /**
     * Parse HTTP response headers and detect content type
     */
    static ResponseMetadata parse_response_headers(std::string_view raw_response) {
        ResponseMetadata metadata;
        
        // Find end of headers
        auto header_end = raw_response.find("\r\n\r\n");
        if (header_end == std::string_view::npos) {
            return metadata;
        }
        
        std::string_view headers_section = raw_response.substr(0, header_end);
        
        // Parse status line
        auto line_end = headers_section.find("\r\n");
        if (line_end != std::string_view::npos) {
            metadata.status_line = std::string(headers_section.substr(0, line_end));
        }
        
        // Parse headers
        size_t pos = line_end + 2;
        while (pos < headers_section.size()) {
            line_end = headers_section.find("\r\n", pos);
            if (line_end == std::string_view::npos) {
                line_end = headers_section.size();
            }
            
            std::string_view line = headers_section.substr(pos, line_end - pos);
            auto colon_pos = line.find(':');
            if (colon_pos != std::string_view::npos) {
                std::string key = std::string(line.substr(0, colon_pos));
                std::string value = std::string(line.substr(colon_pos + 1));
                
                // Trim leading whitespace from value
                size_t start = value.find_first_not_of(" \t");
                if (start != std::string::npos) {
                    value = value.substr(start);
                }
                
                metadata.headers.emplace_back(std::move(key), std::move(value));
                
                // Check Content-Type
                if (key == "Content-Type" || key == "content-type") {
                    metadata.content_type = detect_content_type(value);
                    metadata.has_javascript = (value.find("javascript") != std::string::npos);
                }
                
                // Check Content-Length
                if (key == "Content-Length" || key == "content-length") {
                    try {
                        metadata.content_length = std::stoull(value);
                    } catch (...) {
                        // Ignore parse errors
                    }
                }
            }
            
            pos = line_end + 2;
        }
        
        return metadata;
    }
    
    /**
     * Determine if script engine can be bypassed
     * Returns true if content is pure HTML/CSS without JavaScript
     */
    static bool can_bypass_script_engine(const ResponseMetadata& metadata) {
        // If Content-Type indicates no JavaScript
        if (metadata.content_type == ContentType::HTML || 
            metadata.content_type == ContentType::CSS) {
            return !metadata.has_javascript;
        }
        
        // For unknown types, check if it's binary
        return metadata.content_type == ContentType::Binary;
    }
    
    /**
     * Detect content type from Content-Type header
     */
    static ContentType detect_content_type(std::string_view content_type_header) {
        // Convert to lowercase for comparison
        std::string lower_header(content_type_header);
        for (auto& c : lower_header) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        
        if (lower_header.find("text/html") != std::string::npos) {
            return ContentType::HTML;
        } else if (lower_header.find("text/css") != std::string::npos) {
            return ContentType::CSS;
        } else if (lower_header.find("javascript") != std::string::npos ||
                   lower_header.find("application/x-javascript") != std::string::npos) {
            return ContentType::JavaScript;
        } else if (lower_header.find("application/json") != std::string::npos) {
            return ContentType::JSON;
        } else if (lower_header.find("image/") != std::string::npos ||
                   lower_header.find("video/") != std::string::npos ||
                   lower_header.find("audio/") != std::string::npos ||
                   lower_header.find("application/octet-stream") != std::string::npos) {
            return ContentType::Binary;
        }
        
        return ContentType::Other;
    }
};

} // namespace aetheris::network

#endif // AETHERIS_SOCKET_ABSTRACTION_HPP

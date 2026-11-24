// Row 0.4: what concurrency is worth for a page's linked resources.
//
// Build and run, from the repo root, with no engine library needed - it is raw sockets:
//   zigcxx -std=c++23 -O2 -pthread fetch-cost.cpp -o fetch-cost && ./fetch-cost
//
// The wikipedia page spends about 300 ms per resource and has 188 of them, fetched one at a time.
// This measures the ceiling: the same 188 fetches against one loopback server, serially and with a
// bounded number of workers, at a latency the server is told to model.

#include <atomic>
#include <chrono>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if AETHERIS_PLATFORM_WINDOWS
int main() { return 0; }
#else

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using Clock = std::chrono::steady_clock;

// One HTTP/1.1 GET over a fresh socket, which is what the engine does per resource today. Kept to raw
// sockets so this probe measures the network and the server, not the engine: a version that called
// HttpClient from several threads raced on its shared caches, and the per-thread caches that fix that
// belong with the change this probe is arguing for.
static bool get_once(unsigned short port, int index)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
    struct sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(fd);
        return false;
    }
    std::string const request = "GET /r" + std::to_string(index) + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    if (::send(fd, request.data(), request.size(), 0) < 0) {
        ::close(fd);
        return false;
    }
    char buffer[256];
    auto const received = ::recv(fd, buffer, sizeof(buffer), 0);
    ::close(fd);
    return received > 0;
}

static constexpr int kRequests = 188;
static constexpr int kLatencyMs = 20; // modelled round trip; the real page's is about 300

static double ms_since(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// A server that answers `kRequests` connections, each after a short pause, so a round trip costs
// something and the difference between serial and concurrent is the thing being measured.
static unsigned short start_server(std::thread& thread)
{
    int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        std::perror("socket");
        std::exit(1);
    }
    int reuse = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(listener, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) != 0) {
        std::perror("bind");
        std::exit(1);
    }
    ::listen(listener, 64);
    socklen_t length = sizeof(address);
    ::getsockname(listener, reinterpret_cast<struct sockaddr*>(&address), &length);
    auto const port = ntohs(address.sin_port);

    static constexpr char const* body = "x";
    // A fixed pool of handler threads pulling from a queue. One thread per connection was tried first
    // and hit this box's thread limit (376 threads -> "Resource temporarily unavailable"), which is the
    // same trap the project's own handover lists.
    thread = std::thread([listener]() {
        constexpr int kHandlers = 16;
        std::vector<std::thread> handlers;
        for (int i = 0; i < kHandlers; ++i) {
            handlers.emplace_back([listener]() {
                for (;;) {
                    int connection = ::accept(listener, nullptr, nullptr);
                    if (connection < 0)
                        return;
                    char buffer[1024];
                    ::recv(connection, buffer, sizeof(buffer), 0);
                    std::this_thread::sleep_for(std::chrono::milliseconds(kLatencyMs));
                    std::string response = "HTTP/1.1 200 OK\r\nContent-Type: image/gif\r\nContent-Length: 1\r\n";
                    response += "Connection: close\r\n\r\n";
                    response += body;
                    ::send(connection, response.data(), response.size(), 0);
                    ::close(connection);
                }
            });
        }
        for (auto& handler : handlers)
            handler.join();
    });
    return port;
}

static void run_serial(unsigned short port)
{
    // A fresh connection per request, which is what `Connection: close` forces and what a page with
    // 188 distinct resources mostly sees anyway.
    auto const start = Clock::now();
    int ok = 0;
    for (int i = 0; i < kRequests; ++i) {
        if (get_once(port, i))
            ++ok;
    }
    std::printf("serial        %3d fetches  %8.1f ms  (%d ok)\n", kRequests, ms_since(start), ok);
}

static void run_concurrent(unsigned short port, int workers)
{
    auto const start = Clock::now();
    std::atomic<int> next { 0 };
    std::atomic<int> ok { 0 };
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(workers));
    for (int w = 0; w < workers; ++w) {
        threads.emplace_back([port, &next, &ok]() {
            for (;;) {
                int const i = next.fetch_add(1);
                if (i >= kRequests)
                    break;
                if (get_once(port, i))
                    ++ok;
            }
        });
    }
    for (auto& thread : threads)
        thread.join();
    std::printf("concurrent x%-2d %3d fetches  %8.1f ms  (%d ok)\n", workers, kRequests, ms_since(start), ok.load());
}

int main()
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::thread server_thread;
    auto const port = start_server(server_thread);
    std::printf("loopback server on port %u, %d requests, %d ms modelled latency\n", port, kRequests, kLatencyMs);

    run_serial(port);
    for (int workers : { 2, 4, 6, 8 })
        run_concurrent(port, workers);

    // The server accepts until the process ends, so it is detached rather than joined.
    server_thread.detach();
    std::printf("(ideal serial floor: %d x %d ms = %d ms)\n", kRequests, kLatencyMs, kRequests * kLatencyMs);
    return 0;
}

#endif

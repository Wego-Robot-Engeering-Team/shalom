// Minimal read-only client for the Shalom HMI bridge protocol v1.
//
// This intentionally sends heartbeats only. Motion, mission, arm, and E-Stop
// commands belong in an authenticated application with explicit safety UX;
// they are not suitable as copy-and-paste sample code.

#include "inspection/framing.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {

constexpr int kDefaultPort = 9090;
constexpr auto kHeartbeatPeriod = std::chrono::milliseconds{200};  // protocol: 5 Hz

volatile std::sig_atomic_t gStop = 0;

void onSignal(int) { gStop = 1; }

double nowSeconds()
{
    using clock = std::chrono::system_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

std::string heartbeatHeader(std::int64_t sequence)
{
    std::ostringstream out;
    out << "{\"v\":1,\"t\":\"hb\",\"ts\":" << std::fixed << std::setprecision(3)
        << nowSeconds() << ",\"p\":{\"seq\":" << sequence << "}}";
    return out.str();
}

bool sendAll(int fd, const std::string &bytes)
{
    std::size_t sent = 0;
    while (sent < bytes.size()) {
#ifdef MSG_NOSIGNAL
        const int flags = MSG_NOSIGNAL;
#else
        const int flags = 0;
#endif
        const auto n = ::send(fd, bytes.data() + sent, bytes.size() - sent, flags);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        std::cerr << "send failed: " << std::strerror(errno) << '\n';
        return false;
    }
    return true;
}

int connectTcp(const std::string &host, const std::string &port)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *addresses = nullptr;
    const int lookup = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &addresses);
    if (lookup != 0) {
        std::cerr << "address lookup failed: " << ::gai_strerror(lookup) << '\n';
        return -1;
    }

    int fd = -1;
    for (auto *address = addresses; address != nullptr; address = address->ai_next) {
        fd = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0)
            continue;

#ifdef SO_NOSIGPIPE
        const int enabled = 1;
        (void)::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif

        if (::connect(fd, address->ai_addr, address->ai_addrlen) == 0)
            break;
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(addresses);
    return fd;
}

void usage(const char *program)
{
    std::cerr << "Usage: " << program << " [--host HOST] [--port PORT]\n"
              << "Default: 127.0.0.1:9090\n";
}

}  // namespace

int main(int argc, char **argv)
{
    std::string host = "127.0.0.1";
    std::string port = std::to_string(kDefaultPort);
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--host" || arg == "--port") && i + 1 < argc) {
            (arg == "--host" ? host : port) = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    const int fd = connectTcp(host, port);
    if (fd < 0) {
        std::cerr << "cannot connect to " << host << ':' << port << '\n';
        return 1;
    }
    std::cout << "connected to " << host << ':' << port
              << "; printing received protocol headers (Ctrl-C to stop)\n";

    inspection::FrameDecoder decoder;
    std::int64_t heartbeatSequence = 0;
    auto nextHeartbeat = std::chrono::steady_clock::now();
    char buffer[8192];

    while (!gStop) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextHeartbeat) {
            const auto frame = inspection::encodeFrame(heartbeatHeader(++heartbeatSequence));
            if (!sendAll(fd, frame))
                break;
            nextHeartbeat = now + kHeartbeatPeriod;
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::max(std::chrono::steady_clock::duration::zero(), nextHeartbeat - now));
        pollfd event{fd, POLLIN, 0};
        const int ready = ::poll(&event, 1, static_cast<int>(remaining.count()));
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            std::cerr << "poll failed: " << std::strerror(errno) << '\n';
            break;
        }
        if (ready == 0)
            continue;
        if ((event.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            std::cerr << "bridge disconnected\n";
            break;
        }

        const auto count = ::recv(fd, buffer, sizeof(buffer), 0);
        if (count == 0) {
            std::cerr << "bridge disconnected\n";
            break;
        }
        if (count < 0) {
            if (errno == EINTR)
                continue;
            std::cerr << "receive failed: " << std::strerror(errno) << '\n';
            break;
        }

        decoder.append(buffer, static_cast<std::size_t>(count));
        for (;;) {
            inspection::Frame frame;
            const auto status = decoder.next(frame);
            if (status == inspection::DecodeStatus::NeedMore)
                break;
            if (status != inspection::DecodeStatus::Ok) {
                std::cerr << "invalid protocol frame; closing connection\n";
                gStop = 1;
                break;
            }
            std::cout << frame.header;
            if (!frame.payload.empty())
                std::cout << "  [binary payload: " << frame.payload.size() << " bytes]";
            std::cout << '\n';
        }
    }

    ::close(fd);
    return 0;
}

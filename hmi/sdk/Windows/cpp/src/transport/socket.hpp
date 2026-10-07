// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

// Blocking TCP client socket for the inspection control protocol.
//
// POSIX and Winsock differ in four places and nowhere else that matters here:
// the handle type, the close call, how errors are reported, and whether the
// library needs starting up. Everything else is the same call with the same
// meaning, so one file covers all three platforms rather than three files that
// drift apart.
//
// Header-only and dependency-free, like the framing layer it sits under.

#include <cstddef>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <poll.h>
#  include <fcntl.h>
#  include <sys/time.h>
#  include <sys/socket.h>
#  include <unistd.h>
#  include <cerrno>
#  include <cstring>
#endif

namespace robot_sdk {

#if defined(_WIN32)
using NativeSocket = SOCKET;
inline constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
inline constexpr NativeSocket kInvalidSocket = -1;
#endif

/// Initializes Winsock once per process.
///
/// A no-op elsewhere. Constructing a TcpClient does this for you, so callers
/// only need it directly when they open sockets by some other route.
class SocketLibrary {
public:
    static bool ensureStarted(std::string *err = nullptr)
    {
#if defined(_WIN32)
        static const int result = [] {
            WSADATA data{};
            return ::WSAStartup(MAKEWORD(2, 2), &data);
        }();
        if (result != 0) {
            if (err)
                *err = "WSAStartup 실패: " + std::to_string(result);
            return false;
        }
#else
        (void)err;
#endif
        return true;
    }
};

/// What a read attempt produced.
enum class ReadStatus {
    Data,     ///< bytes were appended to the caller's buffer
    Timeout,  ///< nothing arrived within the deadline; not an error
    Closed,   ///< peer closed the connection
    Error,    ///< socket failure; the connection is unusable
};

/// Blocking TCP client with a poll-based read timeout.
///
/// Deliberately minimal: the protocol needs one connection, ordered bytes, and
/// a way to wake up regularly enough to send heartbeats. Anything richer would
/// be a networking library, and the customer already has one they prefer.
class TcpClient {
public:
    TcpClient() = default;
    ~TcpClient() { close(); }

    TcpClient(const TcpClient &) = delete;
    TcpClient &operator=(const TcpClient &) = delete;

    /// Resolves and connects. Host may be a name or a literal address.
    ///
    /// Every resolved address is tried in turn: a machine that answers on IPv6
    /// but listens on IPv4 would otherwise fail with a misleading error.
    bool connect(const std::string &host, std::uint16_t port, std::string *err = nullptr,
                 int timeoutMs = 5000)
    {
        if (!SocketLibrary::ensureStarted(err))
            return false;
        close();

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        const std::string service = std::to_string(port);
        addrinfo *resolved = nullptr;
        const int rc = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &resolved);
        if (rc != 0 || resolved == nullptr) {
            if (err)
                *err = "주소를 찾을 수 없습니다: " + host;
            return false;
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        for (const addrinfo *it = resolved; it != nullptr; it = it->ai_next) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) break;
            const NativeSocket fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
            if (fd == kInvalidSocket)
                continue;
            if (!setBlocking(fd, false)) { closeHandle(fd); continue; }
            bool connected = ::connect(fd, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0;
            if (!connected) {
#if defined(_WIN32)
                const auto error = ::WSAGetLastError();
                const bool inProgress = error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
                WSAPOLLFD pfd{fd, POLLWRNORM, 0};
                const auto ready = inProgress ? ::WSAPoll(&pfd, 1, static_cast<int>(remaining)) : -1;
                int pendingError = 0, length = sizeof pendingError;
                connected = ready > 0 && ::getsockopt(fd, SOL_SOCKET, SO_ERROR,
                    reinterpret_cast<char *>(&pendingError), &length) == 0 && pendingError == 0;
#else
                const bool inProgress = errno == EINPROGRESS || errno == EINTR;
                pollfd pfd{fd, POLLOUT, 0};
                const auto ready = inProgress ? ::poll(&pfd, 1, static_cast<int>(remaining)) : -1;
                int pendingError = 0;
                socklen_t length = sizeof pendingError;
                connected = ready > 0 && ::getsockopt(fd, SOL_SOCKET, SO_ERROR,
                    &pendingError, &length) == 0 && pendingError == 0;
#endif
            }
            if (connected && setBlocking(fd, true)) {
                fd_ = fd;
                break;
            }
            closeHandle(fd);
        }
        ::freeaddrinfo(resolved);

        if (fd_ == kInvalidSocket) {
            if (err)
                *err = "연결하지 못했습니다: " + host + ":" + service + " (" + lastError() + ")";
            return false;
        }

        // Required by the protocol. Without it Nagle batches small frames and
        // jog commands pick up tens of milliseconds, which is felt directly in
        // manual driving.
        setNoDelay();
        setNoSigPipe();
        setIoTimeout();
        return true;
    }

    bool isConnected() const { return fd_ != kInvalidSocket; }

    /// Sends the whole buffer, looping over partial writes.
    bool sendAll(const std::string &bytes, std::string *err = nullptr)
    {
        if (!isConnected()) { if (err) *err = "client is not connected"; return false; }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
        std::size_t sent = 0;
        while (sent < bytes.size()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                if (err) *err = "socket send timed out";
                return false;
            }
#if defined(_WIN32)
            const int n = ::send(fd_, bytes.data() + sent,
                                 static_cast<int>(bytes.size() - sent), 0);
#elif defined(MSG_NOSIGNAL)
            // Without this a disconnected peer raises SIGPIPE and kills the
            // process instead of returning an error the caller can report.
            const auto n = ::send(fd_, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
#else
            const auto n = ::send(fd_, bytes.data() + sent, bytes.size() - sent, 0);
#endif
            if (n <= 0) {
                if (wouldBlock())
                    continue;
                if (err)
                    *err = "전송 실패: " + lastError();
                return false;
            }
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    /// Waits up to timeoutMs, then appends whatever arrived to out.
    ///
    /// A timeout is a normal outcome, not a failure: the caller uses it to send
    /// the next heartbeat on schedule even when the robot is quiet.
    ReadStatus read(std::string &out, int timeoutMs)
    {
        if (fd_ == kInvalidSocket)
            return ReadStatus::Error;

#if defined(_WIN32)
        WSAPOLLFD pfd{};
        pfd.fd = fd_;
        pfd.events = POLLRDNORM;
        const int ready = ::WSAPoll(&pfd, 1, timeoutMs);
#else
        pollfd pfd{};
        pfd.fd = fd_;
        pfd.events = POLLIN;
        const int ready = ::poll(&pfd, 1, timeoutMs);
#endif
        if (ready == 0)
            return ReadStatus::Timeout;
        if (ready < 0)
            return wouldBlock() ? ReadStatus::Timeout : ReadStatus::Error;

        char buffer[16 * 1024];
#if defined(_WIN32)
        const int n = ::recv(fd_, buffer, static_cast<int>(sizeof buffer), 0);
#else
        const auto n = ::recv(fd_, buffer, sizeof buffer, 0);
#endif
        if (n == 0)
            return ReadStatus::Closed;
        if (n < 0)
            return wouldBlock() ? ReadStatus::Timeout : ReadStatus::Error;

        out.append(buffer, static_cast<std::size_t>(n));
        return ReadStatus::Data;
    }

    void close()
    {
        if (fd_ != kInvalidSocket) {
            closeHandle(fd_);
            fd_ = kInvalidSocket;
        }
    }

    /// Last platform error as text, for messages the operator will read.
    static std::string lastError()
    {
#if defined(_WIN32)
        return "WSA " + std::to_string(::WSAGetLastError());
#else
        return std::strerror(errno);
#endif
    }

private:
    static bool setBlocking(NativeSocket fd, bool blocking)
    {
#if defined(_WIN32)
        u_long mode = blocking ? 0 : 1;
        return ::ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
        const auto flags = ::fcntl(fd, F_GETFL, 0);
        return flags >= 0 && ::fcntl(fd, F_SETFL, blocking ? flags & ~O_NONBLOCK : flags | O_NONBLOCK) == 0;
#endif
    }

    void setIoTimeout()
    {
#if defined(_WIN32)
        const DWORD timeout = 500;
        ::setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout), sizeof timeout);
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof timeout);
#else
        const timeval timeout{0, 500000};
        ::setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
#endif
    }

    static void closeHandle(NativeSocket fd)
    {
#if defined(_WIN32)
        ::closesocket(fd);
#else
        ::close(fd);
#endif
    }

    static bool wouldBlock()
    {
#if defined(_WIN32)
        const int e = ::WSAGetLastError();
        return e == WSAEWOULDBLOCK || e == WSAEINTR;
#else
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
    }

    void setNoDelay()
    {
        const int on = 1;
#if defined(_WIN32)
        ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY,
                     reinterpret_cast<const char *>(&on), sizeof on);
#else
        ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
#endif
    }

    void setNoSigPipe()
    {
#if defined(SO_NOSIGPIPE)
        // macOS has no MSG_NOSIGNAL. Set this per socket so a broken peer is
        // reported as a send error instead of terminating the customer process.
        const int on = 1;
        ::setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
    }

    NativeSocket fd_ = kInvalidSocket;
};

}  // namespace robot_sdk

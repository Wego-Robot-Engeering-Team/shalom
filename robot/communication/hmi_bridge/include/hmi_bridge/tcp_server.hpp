#pragma once

// TCP server for the control station link.
//
// Runs its own thread with poll(), and hands frames to the ROS 2 side through
// a queue rather than by calling back. Callbacks from the network thread into
// node state would need locking everywhere and would eventually be got wrong;
// draining a queue from a timer on the executor thread cannot be.
//
// ONE CLIENT AT A TIME
// --------------------
// A second connection is accepted and immediately closed. The alternative -
// several control stations connected at once - raises a question this system
// has no answer for: if two operators send conflicting commands, which wins?
// Refusing the second connection is the honest behaviour, and the operator
// sees why rather than wondering whose screen is authoritative.
//
// See docs/bridge_protocol.md section 1 for the wire format.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "inspection/framing.hpp"

namespace hmi_bridge {

/// What the server observed since the last drain.
struct LinkEvents {
    bool clientConnected = false;
    bool clientDisconnected = false;

    /// Set when the stream lost synchronisation or declared an impossible
    /// length. The connection has already been closed; the node should report
    /// it, because a corrupted stream means something is wrong with the link
    /// or with one of the two implementations.
    bool protocolError = false;
    std::string protocolErrorDetail;

    /// Frames received, in order.
    std::vector<inspection::Frame> frames;

    bool empty() const
    {
        return !clientConnected && !clientDisconnected && !protocolError
               && frames.empty();
    }
};

class TcpServer {
public:
    TcpServer() = default;
    ~TcpServer();

    TcpServer(const TcpServer &) = delete;
    TcpServer &operator=(const TcpServer &) = delete;

    /// Binds and starts the accept loop. Returns false with a reason in `err`.
    bool start(std::uint16_t port, std::string *err);
    void stop();

    bool isConnected() const { return connected_.load(); }

    /// Queues an already-encoded frame. Safe to call from any thread.
    ///
    /// `lossy` marks a message that may be dropped when the outbound queue is
    /// backed up: a stale pose is worth less than a fresh one, and queueing
    /// them all makes the backlog worse rather than better. Commands and
    /// responses are never lossy.
    void send(std::string encodedFrame, bool lossy = false);

    /// Takes everything observed since the previous call. Intended to be
    /// called from the ROS 2 executor thread.
    LinkEvents drain();

    /// Bytes counters for the diagnostics channel, reset by the caller.
    std::uint64_t rxBytes() const { return rxBytes_.load(); }
    std::uint64_t txBytes() const { return txBytes_.load(); }
    void resetByteCounters();

private:
    void runLoop();
    void closeClient(bool notify);
    void handleReadable();
    void handleWritable();

    /// Outbound backlog beyond which lossy messages are dropped. A control
    /// station that has stopped reading must not be able to make the bridge
    /// grow without bound.
    static constexpr std::size_t kMaxOutboundBytes = 4u * 1024u * 1024u;

    int listenFd_ = -1;
    int clientFd_ = -1;
    int wakeFd_[2] = {-1, -1};   ///< self-pipe so stop() interrupts poll()

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};

    inspection::FrameDecoder decoder_;

    mutable std::mutex outMutex_;
    std::string outBuffer_;

    mutable std::mutex eventMutex_;
    LinkEvents events_;

    std::atomic<std::uint64_t> rxBytes_{0};
    std::atomic<std::uint64_t> txBytes_{0};
};

}  // namespace hmi_bridge

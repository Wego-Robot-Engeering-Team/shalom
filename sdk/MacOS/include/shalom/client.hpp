// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#pragma once

// Protocol client for the Shalom inspection robot, version 1.
//
// Owns the three things every correct client must do and which are easy to get
// subtly wrong:
//
//   * heartbeats at 5 Hz, in both directions, without blocking the read loop
//   * draining every frame a single read delivered, not just the first
//   * pinning the robot identifier and refusing a different one
//
// Deliberately does not parse the JSON payload. Payload shapes are documented
// in state.md and command.md, and every customer already has a JSON library
// they prefer; forcing one into an SDK header is how a dependency conflict
// gets shipped. Frames arrive as envelope text plus binary payload, and the
// caller decodes them with whatever it already uses.
//
// See transport.md for the wire format and errors.md for the codes.

#include "inspection/framing.hpp"
#include "shalom/socket.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

namespace shalom {

/// One decoded message: the JSON envelope as text, plus any binary body.
struct Message {
    std::string envelope;  ///< UTF-8 JSON; see transport.md
    std::string payload;   ///< PNG for map/occupancy and capture/preview; else empty
};

/// Why the client stopped running.
enum class Stop {
    Requested,     ///< the caller's callback asked to stop
    PeerClosed,    ///< the robot closed the connection
    ProtocolError, ///< framing desynchronised; do not reconnect blindly
    SocketError,
};

/// Connects, keeps the heartbeat going, and hands decoded frames to a callback.
///
/// Single-threaded by design. run() blocks and does the timing itself, so the
/// caller does not have to get a second thread right to satisfy the 5 Hz
/// requirement.
class Client {
public:
    /// Called for every frame. Return false to stop the loop.
    using Handler = std::function<bool(const Message &)>;

    bool connect(const std::string &host, std::uint16_t port = 9090,
                 std::string *err = nullptr)
    {
        decoder_.reset();   // leftover bytes would misalign the first frame
        robot_.clear();
        sequence_ = 0;
        return socket_.connect(host, port, err);
    }

    void close() { socket_.close(); }
    bool isConnected() const { return socket_.isConnected(); }

    /// Identifier the robot stamped on its frames, once one has arrived.
    const std::string &robotId() const { return robot_; }

    /// Runs until the handler returns false or the connection ends.
    Stop run(const Handler &onMessage, std::string *err = nullptr)
    {
        using clock = std::chrono::steady_clock;
        auto nextBeat = clock::now();

        for (;;) {
            const auto now = clock::now();
            if (now >= nextBeat) {
                if (!sendHeartbeat(err))
                    return Stop::SocketError;
                nextBeat = now + kHeartbeatPeriod;
            }

            const auto waitMs = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(nextBeat - clock::now())
                    .count());
            std::string chunk;
            switch (socket_.read(chunk, waitMs > 0 ? waitMs : 0)) {
            case ReadStatus::Timeout:
                continue;   // no data; the loop above sends the next heartbeat
            case ReadStatus::Closed:
                return Stop::PeerClosed;
            case ReadStatus::Error:
                if (err)
                    *err = "수신 실패: " + TcpClient::lastError();
                return Stop::SocketError;
            case ReadStatus::Data:
                break;
            }

            decoder_.append(chunk);

            // One read can carry several frames. Handling only the first would
            // never report an error - it would just fall further behind.
            for (;;) {
                inspection::Frame frame;
                const auto status = decoder_.next(frame);
                if (status == inspection::DecodeStatus::NeedMore)
                    break;
                if (status != inspection::DecodeStatus::Ok) {
                    if (err)
                        *err = status == inspection::DecodeStatus::BadMagic
                                   ? "프레임 동기가 깨졌습니다 (LINK_FRAME_CORRUPT)"
                                   : "프레임 크기가 규격을 벗어났습니다 (LINK_FRAME_TOO_LARGE)";
                    return Stop::ProtocolError;
                }

                if (!checkRobot(frame.header, err))
                    return Stop::ProtocolError;

                if (!onMessage(Message{frame.header, frame.payload}))
                    return Stop::Requested;
            }
        }
    }

    /// Sends a command request. The caller supplies the JSON payload text.
    ///
    /// Correlation ids are generated here and returned so the caller can match
    /// the response without inventing its own scheme.
    std::string sendRequest(const std::string &channel, const std::string &payloadJson = "{}",
                            std::string *err = nullptr)
    {
        const std::string id = "c" + std::to_string(++requestId_);
        const std::string header = "{\"v\":1,\"t\":\"req\",\"ch\":\"" + channel
                                   + "\",\"id\":\"" + id + "\",\"ts\":" + timestamp()
                                   + ",\"p\":" + payloadJson + "}";
        if (!socket_.sendAll(inspection::encodeFrame(header), err))
            return {};
        return id;
    }

    /// Publishes on a channel. Used only for cmd/cmd_vel, which carries no
    /// response because a 20 Hz jog stream cannot wait for round trips.
    bool publish(const std::string &channel, const std::string &payloadJson,
                 std::string *err = nullptr)
    {
        const std::string header = "{\"v\":1,\"t\":\"pub\",\"ch\":\"" + channel
                                   + "\",\"ts\":" + timestamp() + ",\"p\":" + payloadJson + "}";
        return socket_.sendAll(inspection::encodeFrame(header), err);
    }

private:
    static constexpr auto kHeartbeatPeriod = std::chrono::milliseconds{200};  // 5 Hz

    static std::string timestamp()
    {
        using clock = std::chrono::system_clock;
        const double s =
            std::chrono::duration<double>(clock::now().time_since_epoch()).count();
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.3f", s);
        return buf;
    }

    bool sendHeartbeat(std::string *err)
    {
        const std::string header = "{\"v\":1,\"t\":\"hb\",\"ts\":" + timestamp()
                                   + ",\"p\":{\"seq\":" + std::to_string(++sequence_) + "}}";
        return socket_.sendAll(inspection::encodeFrame(header), err);
    }

    /// Pins the first robot identifier seen and refuses any other.
    ///
    /// Without this a mis-set address looks like a working system: the screen
    /// fills with plausible telemetry from the wrong machine, and an emergency
    /// stop pressed on it halts a robot nobody is watching.
    bool checkRobot(const std::string &envelope, std::string *err)
    {
        const std::string id = extractString(envelope, "robot");
        if (id.empty())
            return true;
        if (robot_.empty()) {
            robot_ = id;
            return true;
        }
        if (robot_ != id) {
            if (err)
                *err = "다른 로봇이 응답했습니다 (E_ROBOT_MISMATCH): "
                       + robot_ + " -> " + id;
            return false;
        }
        return true;
    }

    /// Pulls one top-level string field out of the envelope.
    ///
    /// Enough for the identity check and nothing more, so that the SDK does not
    /// impose a JSON library on the customer. Payloads are the caller's to
    /// parse with whatever they already use.
    static std::string extractString(const std::string &json, const std::string &key)
    {
        const std::string needle = "\"" + key + "\"";
        const auto at = json.find(needle);
        if (at == std::string::npos)
            return {};
        auto i = json.find(':', at + needle.size());
        if (i == std::string::npos)
            return {};
        while (++i < json.size() && (json[i] == ' ' || json[i] == '\t')) {}
        if (i >= json.size() || json[i] != '"')
            return {};
        const auto begin = ++i;
        while (i < json.size() && json[i] != '"') {
            if (json[i] == '\\')
                ++i;
            ++i;
        }
        return i <= json.size() ? json.substr(begin, i - begin) : std::string{};
    }

    TcpClient socket_;
    inspection::FrameDecoder decoder_;
    std::string robot_;
    std::int64_t sequence_ = 0;
    std::int64_t requestId_ = 0;
};

}  // namespace shalom

// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary
#pragma once
#include "robot_sdk/export.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace robot_sdk {
struct Message {
    std::string envelope; // Complete UTF-8 JSON envelope.
    std::string payload;  // Optional binary body.
    std::string type;
    std::string channel;
    std::string requestId;
    double receivedMonotonicSeconds = 0;
};
struct Response {
    std::string requestId;
    bool ok = false;
    std::string errorCode;
    std::string errorMessage;
    Message message;
};
enum class Stop { Requested, PeerClosed, ProtocolError, SocketError };

/// Single-threaded. poll/run or request must run at least every 200 ms.
class ROBOT_SDK_API Client {
public:
    using Handler = std::function<bool(const Message &)>;
    Client();
    ~Client();
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;
    Client(Client &&) noexcept;
    Client &operator=(Client &&) noexcept;
    bool connect(const std::string &host, std::uint16_t port = 9090,
                 std::string *err = nullptr, int timeoutMs = 5000);
    void close();
    bool isConnected() const;
    const std::string &robotId() const;
    /// Copies the last state publication for this connection.
    std::optional<Message> latest(const std::string &channel) const;
    /// Polls once, sends due heartbeats. Zero timeout is non-blocking.
    bool poll(std::vector<Message> &messages, int timeoutMs = 200, std::string *err = nullptr);
    Stop run(const Handler &onMessage, std::string *err = nullptr);
    /// Returns a send ID, not an acceptance result. No automatic retry.
    std::string sendRequest(const std::string &channel, const std::string &payloadJson = "{}",
                            std::string *err = nullptr, int timeoutMs = 3000);
    std::optional<Response> takeResponse(const std::string &requestId);
    /// Removes local bookkeeping only, without cancelling robot execution.
    void forgetRequest(const std::string &requestId);
    /// nullopt means local/transport/timeout failure; ok=false means robot rejection.
    std::optional<Response> request(const std::string &channel, const std::string &payloadJson = "{}",
                                   int timeoutMs = 3000, const Handler &onMessage = {},
                                   std::string *err = nullptr);
    /// Advanced protocol publication. TCP cmd/cmd_vel is disabled.
    bool publish(const std::string &channel, const std::string &payloadJson, std::string *err = nullptr);
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace robot_sdk

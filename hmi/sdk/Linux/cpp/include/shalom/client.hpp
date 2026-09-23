// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#pragma once

#include "shalom/export.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace shalom {

/// One decoded protocol message. JSON remains text so the customer can use the
/// JSON library already selected by its application.
struct Message {
    std::string envelope;
    std::string payload;
};

enum class Stop { Requested, PeerClosed, ProtocolError, SocketError };

/// TCP bridge client. Its transport, framing and heartbeat implementation are
/// contained in the Shalom SDK binary, not in this public header.
class SHALOM_SDK_API Client {
public:
    using Handler = std::function<bool(const Message &)>;

    Client();
    ~Client();
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;
    Client(Client &&) noexcept;
    Client &operator=(Client &&) noexcept;

    bool connect(const std::string &host, std::uint16_t port = 9090,
                 std::string *err = nullptr);
    void close();
    bool isConnected() const;
    const std::string &robotId() const;

    /// Blocks while maintaining the required 5 Hz heartbeat. Return false from
    /// the handler to stop it normally.
    Stop run(const Handler &onMessage, std::string *err = nullptr);

    /// Sends a request and returns its correlation id. A returned id confirms
    /// bridge acceptance only; process matching `res` and `state/*` frames.
    std::string sendRequest(const std::string &channel, const std::string &payloadJson = "{}",
                            std::string *err = nullptr);

    /// Used for the non-request/response `cmd/cmd_vel` stream.
    bool publish(const std::string &channel, const std::string &payloadJson,
                 std::string *err = nullptr);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace shalom
